# xbox360_avatar_lib

Xbox 360 avatars for homebrew built on [xsdk](../avatar_thrift_store/third_party/xsdk): the player's own avatar, or
NPCs shipped as descriptions, dressed, textured and animated with the console's stock animations, drawn in your
title's own pass. Every asset is read at run time from the console the title runs on; the library ships code only.

GOALS.md says what it is for, SPECS.md holds the format facts and the milestones. Work in progress: the avatar layer
is new (milestone 8) and the interface may still change.

## A first avatar

```c
#include "xsdk/avatar.h"

// After xsdk_gpu_open:
XsdkAvatar *avatar;
if (xsdk_avatar_load_profile(&avatar, 0) != XSDK_AVATAR_OK)
	return 1;
xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

// Each frame, after setting your target and depth state:
xsdk_avatar_update(avatar, dt);
xsdk_avatar_draw(avatar, view_projection); // column-major, depth 0 to 1

// A wave, then back to standing:
xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ONCE);
xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

// It blinks on its own; a face, and talking from a 0-1 loudness:
const XsdkAvatarFace happy = { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY };
xsdk_avatar_set_face(avatar, &happy);
xsdk_avatar_set_talking(avatar, level);

// At the end, after a fence past the last frame that drew it:
xsdk_avatar_close(avatar);
xsdk_avatar_shutdown();
```

`samples/hello` is this as a whole title, in C++ (`xsdk/avatar.hpp`). `samples/viewer` puts several avatars in one
scene: the player's, NPCs from description files, and stock ones. `samples/store` is a small game: walk your
avatar round the Avatar Thrift Store among NPCs, with a walk the library makes in code and an emote wheel.

The avatar's space is metres, y up, facing +z, feet at y 0; place it with `xsdk_avatar_set_transform`. A draw
changes only the shaders, vertex constants 0-222, pixel constants 0-29, vertex stream 0, texture fetch slots 0-5 and
the blend state (left off); depth, culling and multisampling are yours. Avatars are lit per pixel by one light you
set (`xsdk_avatar_set_light`: direction, colour, sky and ground ambient, highlight). `avatar.h` documents every call.

## The three layers

- **The avatar layer** (`XsdkAvatar`): load, play, update, draw, close. What most titles need.
- **The animation layer**: stock animations (`xsdk_avatar_stock_animation`), a player of your own
  (`XsdkAvatarPlayer`), a walk made in code (`xsdk_avatar_animation_walk`: the stock animations have none), poses you
  sample, blend or set bone by bone, drawn with `xsdk_avatar_draw_pose`; the skeleton and skin matrices.
- **The asset layer**: the stock pack, item packages, descriptions, models, textures, skeletons and animations as
  plain structs. No GPU; it builds for the PC too.

## Using it in a title

The library builds with xsdk's toolchain through CMake. In your title's `CMakeLists.txt`:

```cmake
add_subdirectory(<path to xbox360_avatar_lib> xsdk_avatar)
target_link_libraries(<title> PRIVATE xsdk_avatar)
```

It generates its shaders while building, so it needs xsdk's `build/host/libxsc.so` and glslang in
`xenos-toolchain/tools/glslang` (xsdk's README).

## Building here

In WSL, from this folder:

```
./build.sh [<xsdk checkout>]   # the library and the samples: build/console/samples/*/*.xex
./build.sh pc                  # the PC tests: build/pc/avatar_dump, player_check, outfit_check
```

Without an argument it uses Avatar Thrift Store's xsdk submodule (`../avatar_thrift_store/third_party/xsdk`).

## Making animations

1. Run `samples/rig_export` on the console. It writes rigs to animate beside its `.xex`: `rig_male.glb` and
   `rig_female.glb` (the bare body and head) and `rig_mine.glb` (your signed-in avatar in its clothes), showing on
   screen how each went. Each is the library's 71-bone skeleton in the T-pose with 360sona's bone names, textured
   (`xsdk_avatar_export_*` in a title of your own). They hold the console's meshes, so keep them out of the
   repository.
2. Import it into Blender (or any 3D program that reads glTF), animate, and export glTF with the animation.
3. `python3 tools/anim_convert.py <export.glb> <out.xaan>` turns the export into the library's own file, which
   `xsdk_avatar_animation_load` reads (the store sample plays files from an `Animations` folder beside it).

## Tests

- `build/pc/player_check`: the player's cross-fades, queueing and timing, on made-up animations.
- `build/pc/walk_check <FFFE07DF00000002>`: the walk made in code on both bodies: it loops, the standing foot stays
  on the floor without sliding, and the legs are symmetric.
- `tools/anim_convert.py <in.glb> <out.xaan>` and `build/pc/anim_check <FFFE07DF00000002> <out.xaan> "<stock name>"`: an
  animation from a glTF export (360sona's, say) converted into the library's own file and checked against the stock
  animation it holds.
- `build/pc/outfit_check <FFFE07DF00000002> <description> <package folder> ...`: an avatar's parts put together as
  on the console (stock parts, item packages, what each hides of the body), from your own files. With `stock` for the
  description: an avatar of stock parts with face features. With `export <out.glb> male|female|stock|<description>`:
  the console's rig export, run on the PC.
- `build/pc/avatar_dump` with `research/check_c.py`: the decoders against the Python prototypes, on your own console
  files (the stock pack, item packages). See the comments at the top of both.

Nothing from the console goes into this repository: no packs, packages, decoded assets or renders.

## Licence

To be MIT, as xsdk is (SPECS.md, section 1.3); there is no LICENSE file yet. Written from public reverse
engineering and console runs (SPECS.md, section 2); no Microsoft code or XDK material.
