# xbox360_avatar_lib

`xbox360_avatar_lib` is a clean-room C17 library for rendering and animating Xbox 360 Avatars in homebrew titles
built on [xsdk](https://github.com/xenosworks/xsdk). With a few calls, your game can load the signed-in player's
avatar from their profile, spawn NPCs from 1000-byte description files (created on a console or in
[360sona](https://360sona.com/)), or assemble stock avatars by part name—complete with dashboard-accurate clothing,
custom colours, facial expressions, automatic blinking, and voice-driven mouth movement.

Every avatar asset is decoded at run time from the console the title runs on: the library reads the system's stock
avatar pack (`\SEP`) and searches all connected storage devices (hard drive, USB drives, onboard flash, and memory
units) for installed marketplace and award items. For motion, it plays the console's 60 stock emotes and idles with
cross-fading and bone layering, and includes its own MIT-licensed `walk`, `run`, and `jump` animations—plus a
Blender `.glb` rig exporter and converter for creating custom animations.

New here? Start with [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) and `samples/hello_avatar_world`.

## A first avatar

```c
#include "xsdk/avatar.h"

// After xsdk_gpu_open: load an avatar (profile, 1000-byte description, or stock part names):
XsdkAvatar *avatar;
if (xsdk_avatar_load_profile(&avatar, 0) != XSDK_AVATAR_OK)
	return 1;
XsdkAvatarLocomotion me;
xsdk_avatar_locomotion_load(&me, avatar);   // the library's walk, run and jump, fitted to this avatar
xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

// Each frame, inside your render pass (after setting your target and depth state):
while (running) {
	xsdk_avatar_locomotion_drive(avatar, &me, dx, dz, run_held, jump_pressed, dt, NULL, NULL);
	xsdk_avatar_update(avatar, dt);

	// A camera 7 m away and above, 0.7 radians high, seeing 0.1 to 30 m:
	const float eye[3] = { 0.0f, 3.5f, 7.0f }, target[3] = { 0.0f, 0.6f, 0.0f };
	float view_projection[16];
	xsdk_avatar_camera(view_projection, eye, target, 0.7f, aspect, 0.1f, 30.0f);
	xsdk_avatar_draw_shadow(avatar, view_projection);
	xsdk_avatar_draw(avatar, view_projection);
}

// At the end, after a fence past the last frame that drew it:
xsdk_avatar_close(avatar);
xsdk_avatar_locomotion_free(&me);
xsdk_avatar_shutdown();
```

### Emotes, faces and NPCs

```c
// Play a one-shot wave, then cross-fade back to standing:
xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ONCE);
xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

// Set a facial expression and drive the mouth from a 0-1 loudness (it blinks on its own):
const XsdkAvatarFace happy = { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY };
xsdk_avatar_set_face(avatar, &happy);
xsdk_avatar_set_talking(avatar, level);

// Load NPCs or fallbacks (missing marketplace items fall back to stock parts automatically):
xsdk_avatar_load_description(&npc, description_1000_bytes);  // from probe_manifest or 360sona.com
xsdk_avatar_load_stock(&fallback, part_names, part_count);   // from stock pack record names
```

The library is C17; C++ titles call the same C API. The avatar's space is metres, y up, facing +z, feet at y 0;
`xsdk_avatar_placement` makes a world matrix from a position and a heading for `xsdk_avatar_set_transform` (the
locomotion does it for you). A draw changes only the shaders, vertex constants 0-222, pixel constants 0-29, vertex
stream 0, texture fetch slots 0-5 and the blend state (left off); the target, depth, culling and multisampling are
yours. Avatars are lit per pixel by one light you set (`xsdk_avatar_set_light`). `include/xsdk/avatar.h` documents
every call.

## The samples

| Sample | What it shows |
|---|---|
| `samples/hello_avatar_world` | your avatar on a plain stage: walking, running and jumping, with its shadow. The first one to read and run. |
| `samples/store` | a small game: walk your avatar round the Avatar Thrift Store among NPCs, with an emote wheel, your own animations, anti-aliasing in bands and screenshots. |
| `samples/rig_export` | writes your avatar as a rig to animate in Blender (see "Making animations"). |

## The three layers

- **The avatar layer** (`XsdkAvatar`, console only): load, play, update, draw, close; faces, talking, the light,
  the shadow, and locomotion on top.
- **The animation layer** (PC too): stock animations (`xsdk_avatar_stock_animation`), a player of your own
  (`XsdkAvatarPlayer`), the library's own walk, run and jump (`xsdk_avatar_animation_load_walk`, `_run`, `_jump`), the
  locomotion that plays them (`xsdk_avatar_locomotion_*`), poses you sample, blend or set bone by bone, the skeleton
  and skin matrices; and the camera and placement helpers.
- **The asset layer** (PC too): the stock pack, item packages, descriptions, models, textures, skeletons and
  animations as plain structs, decoded as [docs/FORMATS.md](docs/FORMATS.md) describes.

## Using it in a title

The library has no xsdk of its own: it builds with your title's xsdk, through CMake under xsdk's toolchain file.
In your title's `CMakeLists.txt`:

```cmake
add_subdirectory(<path to xbox360_avatar_lib> xsdk_avatar)
target_link_libraries(<title> PRIVATE xsdk_avatar m)
```

It generates its shaders while building, so it needs xsdk's `build/host/libxsc.so` (`make` in xsdk) and glslang in
`<xsdk>/xenos-toolchain/tools/glslang` (xsdk's README). Last tested with xsdk at commit `01c7a43`.

## Building this repository

In WSL, from this folder, naming an xsdk checkout:

```
./build.sh <xsdk checkout>      # the library and the samples: build/console/samples/*/*.xex
./build.sh pc <xsdk checkout>   # the PC tools and checks: build/pc/
```

## Making animations

1. Run `samples/rig_export` on the console. It writes rigs to animate beside its `.xex`: `rig_male.glb` and
   `rig_female.glb` (the bare body and head) and `rig_mine.glb` (your signed-in avatar in its clothes). Each is the
   library's 71-bone skeleton in the T-pose with 360sona's bone names, textured. They hold the console's meshes:
   keep them out of any repository.
2. Import one into Blender (or any 3D program that reads glTF), animate, and export glTF with the animation. Legs
   baked from an IK rig often twist the thighs unseen: `tools/blender_legs.py` straightens them, and
   `tools/blender_life.py` and `tools/blender_detail.py` are the passes the library's own clips went through (each
   file says how to run it inside Blender).
3. `python3 tools/anim_convert.py <export.glb> <out.xaan>` turns the export into the library's own file, which
   `xsdk_avatar_animation_load` reads (the store sample plays files from an `Animations` folder beside it).
   `build/pc/walk_check <pack> clip <walk.xaan>` checks a walk of your own: no foot sliding or sinking, no twist.

## Checks on the PC

They run on your own console files, never on files in this repository:

- `tools/probe_pack` copies the stock pack (`FFFE07DF00000002`) off a running console, where FTP cannot open `\SEP`.
- `tools/probe_manifest` dumps each signed-in profile's 1000-byte avatar description (`manifest_<n>.bin`), for
  `outfit_check` or for shipping as an NPC.

`./build.sh pc` takes the compiler from `$CC` (gcc if unset). CI (`.github/workflows/ci.yml`) builds them all with GCC
and Clang on every push and pull request, runs `player_check` and `camera_check` (the two that need no console files),
and compiles the Python tools; the console build and the checks on console files stay local.

- `player_check`: the player's cross-fades, queueing and timing, on made-up animations.
- `camera_check`: the camera and placement helpers.
- `locomotion_check <pack>`: walking, running, turning, collisions and the jump, frame by frame.
- `walk_check <pack> [walk | run | jump | clip <file>]`: the library's clips (or yours) by the skinned feet.
- `anim_check <pack> <file.xaan> "<stock name>"`: a converted glTF animation against the stock one it holds.
- `outfit_check <pack> <description> <package folder> ...`: an avatar's parts put together as on the console; with
  `stock` or `hat`, stock avatars (face features; hair under a hat); with `export <out.glb> ...`, the rig export.
- `alloc_check <pack>` and `outfit_alloc_check <pack> alloc <description> ...`: every load with its 1st, 2nd, 3rd,
  ... allocation failing, built with AddressSanitizer.
- `avatar_dump` with `research/check_c.py`: the decoders against the Python prototypes in `research/`.

## Where the knowledge comes from

[docs/FORMATS.md](docs/FORMATS.md) holds everything known about the avatar files, each fact with its evidence;
[EVIDENCE.md](EVIDENCE.md) names the sources: sample files by hash, console runs, public reverse engineering.
[GOALS.md](GOALS.md) says what the library is for. Nothing from the console goes into this repository: no packs,
packages, decoded assets or renders.

## Licence

MIT (LICENSE). Written from samples, console runs and public reverse engineering; no Microsoft code or XDK material.
