#!/usr/bin/env python3
"""Generate the store sample's shaders (store_shaders.h) with the library's generator (tools/gen_shaders.py), lit by
the same LIGHTING as the avatars so that the store and the avatars match:

  python3 store_shaders.py <xsdk checkout> <work dir> > store_shaders.h     (in WSL, as tools/gen_shaders.py)

- store_vertex: positions placed (scaled by place.w, moved by place.xyz), then through the view-projection; the world
  position, normal, colour and material passed on;
- store_pixel: the vertex colour, patterned by the material's weights (one at a time): x plank floor, y brick,
  z wall boards, all lit; w glow, not lit (the neon sign, screens, lamps, and the screen overlay). Alpha is the
  colour's, for what is drawn blended;
- shadow_pixel: a soft round shadow under an avatar, darkest at the centre.

Patterns are worked out from the world position: the floor's planks from x and z, the walls' bricks and boards
along the wall (x + z: one of them is constant on a wall) and up it (y).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gen_shaders  # noqa: E402

VERTEX = """#version 450
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_material;
layout(location = 0) out vec4 v_world;
layout(location = 1) out vec4 v_normal;
layout(location = 2) out vec4 v_color;
layout(location = 3) out vec4 v_material;
layout(push_constant) uniform Push { mat4 view_projection; vec4 place; } pc;

void main()
{
	vec3 world = a_position * pc.place.www + pc.place.xyz;
	gl_Position = pc.view_projection * vec4(world, 1.0);
	v_world = vec4(world, 1.0);
	v_normal = vec4(a_normal, 0.0);
	v_color = a_color;
	v_material = a_material;
}
"""

# xost refuses a vector constant as a GLSL.std.450 call's operand (SPECS.md, section 6, "%154 is not a constant
# scalar"), so blends with a constant colour are written out as arithmetic, and bounds are scalars.
PIXEL = """#version 450
layout(location = 0) in vec4 v_world;
layout(location = 1) in vec4 v_normal;
layout(location = 2) in vec4 v_color;
layout(location = 3) in vec4 v_material;
layout(location = 0) out vec4 o_color;
layout(push_constant) uniform Push { LIGHT_MEMBERS } pc;

void main()
{
	vec3 n = normalize(v_normal.xyz);
	vec3 base = v_color.rgb;
	// The floor's planks: rows 0.2 m wide across z, 1.3 m long along x, the ends staggered row to row.
	vec2 fp = v_world.zx * vec2(5.0, 0.769);
	vec2 fq = fp + floor(fp).xx * vec2(0.0, 0.37);
	vec2 fs = fract(fq);
	vec2 fm = clamp((fs - vec2(0.035, 0.012)) * vec2(60.0, 200.0), 0.0, 1.0);
	vec2 fv = fract(floor(fq) * vec2(0.618, 0.382));
	vec3 planks = base * ((fv.xxx + fv.yyy) * 0.15 + 0.85) * (fm.xxx * fm.yyy * 0.5 + 0.5);
	// Along a wall, and up it.
	vec2 wall = v_world.xy + v_world.zz * vec2(1.0, 0.0);
	// Bricks 0.24 by 0.08 m, every other row moved by half a brick, in pale mortar.
	vec2 bp = wall * vec2(4.1667, 12.5);
	vec2 bq = bp + floor(bp).yy * vec2(0.5, 0.0);
	vec2 bs = fract(bq);
	vec2 bm = clamp((bs - vec2(0.05, 0.12)) * vec2(40.0, 25.0), 0.0, 1.0);
	vec2 bv = fract(floor(bq) * vec2(0.618, 0.382));
	vec3 mortar = vec3(0.72, 0.69, 0.64);
	vec3 brick = mortar + (base * ((bv.xxx + bv.yyy) * 0.2 + 0.8) - mortar) * (bm.xxx * bm.yyy);
	// Wall boards 0.16 m high, 1.25 m long, the ends staggered.
	vec2 wp = wall * vec2(0.8, 6.25);
	vec2 wq = wp + floor(wp).yy * vec2(0.43, 0.0);
	vec2 ws = fract(wq);
	vec2 wm = clamp((ws - vec2(0.01, 0.04)) * vec2(150.0, 50.0), 0.0, 1.0);
	vec2 wv = fract(floor(wq) * vec2(0.618, 0.382));
	vec3 boards = base * ((wv.xxx + wv.yyy) * 0.15 + 0.85) * (wm.xxx * wm.yyy * 0.45 + 0.55);
	vec4 m = v_material;
	vec3 color = base * (1.0 - m.xxx - m.yyy - m.zzz - m.www) + planks * m.xxx + brick * m.yyy + boards * m.zzz;
LIGHTING
	vec3 lit = color * light + shine;
	vec3 shown = lit + (base - lit) * m.www;
	o_color = vec4(shown, 0.0) + vec4(0.0, 0.0, 0.0, 1.0) * v_color.aaaa;
}
""".replace("LIGHT_MEMBERS", gen_shaders.LIGHT_MEMBERS).replace("LIGHTING\n", gen_shaders.LIGHTING)

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
    ("store_vertex", "vert", VERTEX),
    ("store_pixel", "frag", PIXEL),
    ("shadow_pixel", "frag", SHADOW_PIXEL),
)

if __name__ == "__main__":
    print(gen_shaders.generate(sys.argv[1], sys.argv[2], SHADERS, "STORE_SHADERS_H", "samples/store/store_shaders.py"))
