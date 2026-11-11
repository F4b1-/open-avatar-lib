#!/usr/bin/env python3
"""Generate the avatar library's shaders (avatar_shaders.h, for src/avatar_render.c) from GLSL, as xsdk's
tools/gen_draw_cost_shaders.py does.

Each GLSL source goes through xost's glslang to SPIR-V (Vulkan rules), then through xost's compiler at xsdk's register
split with the optimiser on. Prints a C header with each container, the literals the title writes into constant
registers, the first register of its push constants, and the fetch constant slot of each sampler by binding.

  python3 gen_shaders.py <xsdk checkout> <work dir> > avatar_shaders.h     (in WSL; the work dir on a Windows drive,
                                                                            as glslang is xost's Windows build)

The shaders follow SPECS.md, section 3.6:
- avatar_vertex: positions and normals skinned (four bones a vertex, each bone's 3x4 matrix in the push constants,
  read at the vertex's bone index; SPECS.md, section 4.4), then through the mvp matrix, the normal turned by the
  world matrix into world space for the pixel shaders, six texture coordinate sets passed on (the Head uses six;
  items three, in base, mask, decal order);
- avatar_item_pixel: lerp(base, mask.r * C1 + mask.g * C2 + mask.b * C3, mask.a), then the decal by its alpha, or
  a reflection map looked up from the normal in view space by its strength (binding 4 of SPECS.md, inferred);
- avatar_head_pixel: the skin colour, then each of the six face layers, r * A + g * B + b * C, by its alpha;
- both lit per pixel the same way (LIGHTING, SPECS.md section 5.3): a light wrapped round the surface, an ambient
  blended from the ground's colour below to the sky's above, and a highlight towards the camera;
- avatar_shadow_vertex and avatar_shadow_pixel: a soft round shadow on the ground under an avatar
  (xsdk_avatar_draw_shadow), a unit square placed and scaled, darkest at its middle.
"""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REGISTERS = 64  # xsdk's split (xsdk:src/gpu_setup.c)

VERTEX = """#version 450
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec2 a_uv1;
layout(location = 4) in vec2 a_uv2;
layout(location = 5) in vec2 a_uv3;
layout(location = 6) in vec2 a_uv4;
layout(location = 7) in vec2 a_uv5;
// Skinning: per vertex four bones, each as its first row in bones[] (bone index times 3), and their weights.
layout(location = 8) in float a_bone0;
layout(location = 9) in float a_bone1;
layout(location = 10) in float a_bone2;
layout(location = 11) in float a_bone3;
layout(location = 12) in vec4 a_weights;
layout(location = 0) out vec4 v_normal;
layout(location = 1) out vec2 v_uv0;
layout(location = 2) out vec2 v_uv1;
layout(location = 3) out vec2 v_uv2;
layout(location = 4) out vec2 v_uv3;
layout(location = 5) out vec2 v_uv4;
layout(location = 6) out vec2 v_uv5;
// world: the avatar's world matrix, for normals. bones: per bone its skin matrix, three rows of
// [rotation | translation] (xsdk_avatar_skin_matrices), 71 bones.
layout(push_constant) uniform Push { mat4 mvp; mat4 world; vec4 bones[213]; } pc;

void main()
{
	vec4 p = vec4(a_position, 1.0);
	vec4 d = vec4(a_normal, 0.0);
	int i0 = int(a_bone0);
	int i1 = int(a_bone1);
	int i2 = int(a_bone2);
	int i3 = int(a_bone3);
	vec3 p0 = vec3(dot(pc.bones[i0], p), dot(pc.bones[i0 + 1], p), dot(pc.bones[i0 + 2], p));
	vec3 p1 = vec3(dot(pc.bones[i1], p), dot(pc.bones[i1 + 1], p), dot(pc.bones[i1 + 2], p));
	vec3 p2 = vec3(dot(pc.bones[i2], p), dot(pc.bones[i2 + 1], p), dot(pc.bones[i2 + 2], p));
	vec3 p3 = vec3(dot(pc.bones[i3], p), dot(pc.bones[i3 + 1], p), dot(pc.bones[i3 + 2], p));
	vec3 n0 = vec3(dot(pc.bones[i0], d), dot(pc.bones[i0 + 1], d), dot(pc.bones[i0 + 2], d));
	vec3 n1 = vec3(dot(pc.bones[i1], d), dot(pc.bones[i1 + 1], d), dot(pc.bones[i1 + 2], d));
	vec3 n2 = vec3(dot(pc.bones[i2], d), dot(pc.bones[i2 + 1], d), dot(pc.bones[i2 + 2], d));
	vec3 n3 = vec3(dot(pc.bones[i3], d), dot(pc.bones[i3 + 1], d), dot(pc.bones[i3 + 2], d));
	vec3 skinned = a_weights.xxx * p0 + a_weights.yyy * p1 + a_weights.zzz * p2 + a_weights.www * p3;
	vec3 normal = a_weights.xxx * n0 + a_weights.yyy * n1 + a_weights.zzz * n2 + a_weights.www * n3;
	gl_Position = pc.mvp * vec4(skinned, 1.0);
	v_uv0 = a_uv0;
	v_uv1 = a_uv1;
	v_uv2 = a_uv2;
	v_uv3 = a_uv3;
	v_uv4 = a_uv4;
	v_uv5 = a_uv5;
	v_normal = vec4((pc.world * vec4(normal, 0.0)).xyz, 0.0);
}
"""

# The lighting both pixel shaders share, after their own push constants (avatar_render.c writes them per draw):
# light_dir towards the light in world space, w how far it wraps round (0 a plain Lambert term); light_color its
# colour, w the highlight's strength; sky and ground the ambient from above and below; view_* the camera's right, up
# and forward axes in world space. Reads n (the unit normal in world space); gives light (what multiplies the
# surface's colour) and shine (the highlight, added).
LIGHT_MEMBERS = "vec4 light_dir; vec4 light_color; vec4 sky; vec4 ground; vec4 view_right; vec4 view_up; " \
                "vec4 view_forward;"

LIGHTING = """
	vec3 h = normalize(pc.light_dir.xyz - pc.view_forward.xyz);
	vec4 t = vec4(dot(n, pc.light_dir.xyz), dot(n, h), 0.0, 0.0);
	vec4 wrap = pc.light_dir.wwww;
	vec4 lambert = clamp((t + wrap) / (wrap + 1.0), 0.0, 1.0);
	vec4 facing = clamp(t * 4.0, 0.0, 1.0);
	vec4 s1 = max(t, 0.0);
	vec4 s2 = s1 * s1;
	vec4 s4 = s2 * s2;
	vec4 s8 = s4 * s4;
	vec4 s16 = s8 * s8;
	vec3 ambient = mix(pc.ground.rgb, pc.sky.rgb, n.yyy * 0.5 + 0.5);
	vec3 light = ambient + pc.light_color.rgb * lambert.xxx;
	vec3 shine = pc.light_color.rgb * pc.light_color.www * s16.yyy * facing.xxx;
"""

ITEM_PIXEL = """#version 450
layout(location = 0) in vec4 v_normal;
layout(location = 1) in vec2 v_uv0;
layout(location = 2) in vec2 v_uv1;
layout(location = 3) in vec2 v_uv2;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_mask;
layout(set = 0, binding = 2) uniform sampler2D u_decal;
// c1-c3: the custom colours; reflection.x 1 if u_decal is a reflection map, reflection.y its strength.
layout(push_constant) uniform Push { vec4 c1; vec4 c2; vec4 c3; vec4 reflection; LIGHT_MEMBERS } pc;

void main()
{
	vec4 b = texture(u_base, v_uv0);
	vec3 color = b.rgb;
	vec4 m = texture(u_mask, v_uv1);
	vec3 tint = m.rrr * pc.c1.rgb + m.ggg * pc.c2.rgb + m.bbb * pc.c3.rgb;
	color = mix(color, tint, m.aaa);
	vec3 n = normalize(v_normal.xyz);
	// A reflection map is looked up by the normal as the camera sees it.
	vec2 seen = vec2(dot(n, pc.view_right.xyz), dot(n, pc.view_up.xyz));
	vec2 sphere = seen * vec2(0.5, -0.5) + vec2(0.5, 0.5);
	vec4 d = texture(u_decal, mix(v_uv2, sphere, pc.reflection.xx));
	color = mix(color, d.rgb, mix(d.aaa, pc.reflection.yyy, pc.reflection.xxx));
LIGHTING
	// The base texture's alpha, for see-through parts drawn blended (sunglasses lenses).
	o_color = vec4(color * light + shine, 0.0) + b.aaaa * vec4(0.0, 0.0, 0.0, 1.0);
}
""".replace("LIGHT_MEMBERS", LIGHT_MEMBERS).replace("LIGHTING\n", LIGHTING)

HEAD_PIXEL = """#version 450
layout(location = 0) in vec4 v_normal;
layout(location = 1) in vec2 v_uv0;
layout(location = 2) in vec2 v_uv1;
layout(location = 3) in vec2 v_uv2;
layout(location = 4) in vec2 v_uv3;
layout(location = 5) in vec2 v_uv4;
layout(location = 6) in vec2 v_uv5;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_layer0;
layout(set = 0, binding = 1) uniform sampler2D u_layer1;
layout(set = 0, binding = 2) uniform sampler2D u_layer2;
layout(set = 0, binding = 3) uniform sampler2D u_layer3;
layout(set = 0, binding = 4) uniform sampler2D u_layer4;
layout(set = 0, binding = 5) uniform sampler2D u_layer5;
// Per layer, the colours its red, green and blue stand for.
layout(push_constant) uniform Push { vec4 skin; vec4 colors[18]; LIGHT_MEMBERS } pc;

void main()
{
	vec4 t0 = texture(u_layer0, v_uv0);
	vec4 t1 = texture(u_layer1, v_uv1);
	vec4 t2 = texture(u_layer2, v_uv2);
	vec4 t3 = texture(u_layer3, v_uv3);
	vec4 t4 = texture(u_layer4, v_uv4);
	vec4 t5 = texture(u_layer5, v_uv5);
	vec3 c0 = pc.skin.rgb;
	vec3 c1 = mix(c0, t0.rrr * pc.colors[0].rgb + t0.ggg * pc.colors[1].rgb + t0.bbb * pc.colors[2].rgb, t0.aaa);
	vec3 c2 = mix(c1, t1.rrr * pc.colors[3].rgb + t1.ggg * pc.colors[4].rgb + t1.bbb * pc.colors[5].rgb, t1.aaa);
	vec3 c3 = mix(c2, t2.rrr * pc.colors[6].rgb + t2.ggg * pc.colors[7].rgb + t2.bbb * pc.colors[8].rgb, t2.aaa);
	vec3 c4 = mix(c3, t3.rrr * pc.colors[9].rgb + t3.ggg * pc.colors[10].rgb + t3.bbb * pc.colors[11].rgb, t3.aaa);
	vec3 c5 = mix(c4, t4.rrr * pc.colors[12].rgb + t4.ggg * pc.colors[13].rgb + t4.bbb * pc.colors[14].rgb, t4.aaa);
	vec3 c6 = mix(c5, t5.rrr * pc.colors[15].rgb + t5.ggg * pc.colors[16].rgb + t5.bbb * pc.colors[17].rgb, t5.aaa);
	vec3 n = normalize(v_normal.xyz);
LIGHTING
	o_color = vec4(c6 * light + shine, 1.0);
}
""".replace("LIGHT_MEMBERS", LIGHT_MEMBERS).replace("LIGHTING\n", LIGHTING)


SHADOW_VERTEX = """#version 450
layout(location = 0) in vec3 a_position;
layout(location = 0) out vec4 v_world;
// place: where the shadow lies (xyz) and its radius (w): the unit square round the origin scaled and moved there.
layout(push_constant) uniform Push { mat4 view_projection; vec4 place; } pc;

void main()
{
	vec3 world = a_position * pc.place.www + pc.place.xyz;
	gl_Position = pc.view_projection * vec4(world, 1.0);
	v_world = vec4(world, 1.0);
}
"""

# A soft round shadow, darkest at the middle, fading to nothing at the radius; drawn blended by its alpha.
SHADOW_PIXEL = """#version 450
layout(location = 0) in vec4 v_world;
layout(location = 0) out vec4 o_color;
// center: the shadow's middle (x, z) and radius (w); color: its colour and its strength at the middle (a).
layout(push_constant) uniform Push { vec4 center; vec4 color; } pc;

void main()
{
	vec2 d = (v_world.xz - pc.center.xz) / pc.center.ww;
	vec4 r = vec4(dot(d, d));
	vec4 a = clamp(1.0 - r, 0.0, 1.0);
	o_color = vec4(pc.color.rgb, 0.0) + vec4(0.0, 0.0, 0.0, 1.0) * (a * a * pc.color.aaaa);
}
"""

SHADERS = (
    ("avatar_vertex", "vert", VERTEX),
    ("avatar_item_pixel", "frag", ITEM_PIXEL),
    ("avatar_head_pixel", "frag", HEAD_PIXEL),
    ("avatar_shadow_vertex", "vert", SHADOW_VERTEX),
    ("avatar_shadow_pixel", "frag", SHADOW_PIXEL),
)


def spirv_of(glslang, work, name, stage, source):
    with tempfile.TemporaryDirectory(dir=work) as directory:
        glsl = Path(directory) / f"{name}.{stage}"
        glsl.write_text(source)
        output = Path(directory) / f"{name}.spv"
        result = subprocess.run([str(glslang), "-V", glsl.name, "-o", output.name], cwd=directory, capture_output=True,
                                text=True)
        if result.returncode != 0:
            sys.exit(f"glslang failed on {name}:\n{result.stdout}{result.stderr}")
        return output.read_bytes()


def generate(xsdk, work, shaders, guard, generator):
    """The C header for shaders, (name, stage, GLSL) each, compiled as the module docstring says."""
    xsdk, work = Path(xsdk).resolve(), Path(work).resolve()
    sys.path.insert(0, str(xsdk / "xenos-toolchain"))
    import xsc.native as native  # noqa: E402
    glslang = xsdk / "xenos-toolchain" / "tools" / "glslang" / "bin" / "glslang.exe"
    work.mkdir(parents=True, exist_ok=True)

    out = [f"#ifndef {guard}", f"#define {guard}", "",
           "#include <stddef.h>", "#include <stdint.h>", "",
           f"// Generated by {generator}; don't edit.", "",
           "// A literal the shader reads from a constant register, which the title writes.",
           "typedef struct {\n\tuint32_t reg;\n\tfloat values[4];\n} AvatarShaderLiteral;", "",
           "typedef struct {\n\tconst uint32_t *container;\n\tuint32_t container_bytes;\n"
           "\tconst AvatarShaderLiteral *literals;\n\tuint32_t literal_count;\n"
           "\t// The first register of the push constants; the fetch constant slot of each sampler, by binding.\n"
           "\tuint32_t push_register;\n\tuint32_t sampler_count;\n\tuint32_t samplers[8];\n} AvatarShaderCode;", ""]
    table = []
    for name, stage, source in shaders:
        container, manifest, resources = native.compile_spirv(spirv_of(glslang, work, name, stage, source),
                                                              REGISTERS, REGISTERS, optimize=True)
        words = struct.unpack(f">{len(container) // 4}I", container)
        out.append(f"static const uint32_t {name}_container[] = {{")
        out += [f"\t0x{w:08X}," for w in words]
        out.append("};\n")
        literals = []
        for need in manifest:
            if len(need) != 2:
                sys.exit(f"{name}: a need other than a literal, {need!r}")
            literals.append(need)
        if literals:
            out.append(f"static const AvatarShaderLiteral {name}_literals[] = {{")
            for reg, values in literals:
                out.append(f"\t{{ {reg}, {{ {', '.join(repr(float(v)) + 'f' for v in values)} }} }},")
            out.append("};\n")
        push = [r for r in resources if r[0] == "push"]
        samplers = sorted((r for r in resources if r[0] == "sampler"), key=lambda r: r[2])
        if len(push) != 1 or [r[2] for r in samplers] != list(range(len(samplers))):
            sys.exit(f"{name}: expected one push block and samplers bound 0 up, got {resources!r}")
        slots = ", ".join(str(r[3]) for r in samplers) or "0"
        table.append(f"static const AvatarShaderCode {name} = {{ {name}_container, {len(container)}, "
                     f"{f'{name}_literals' if literals else 'NULL'}, {len(literals)}, {push[0][3]}, "
                     f"{len(samplers)}, {{ {slots} }} }};")
        print(f"{name}: {len(container)} bytes, {len(literals)} literals, push at {push[0][3]}, "
              f"samplers {[r[3] for r in samplers]}", file=sys.stderr)
    out += table
    out.append("\n#endif")
    return "\n".join(out)


def main():
    print(generate(sys.argv[1], sys.argv[2], SHADERS, "XSDK_AVATAR_SHADERS_H", "tools/gen_shaders.py"))


if __name__ == "__main__":
    main()
