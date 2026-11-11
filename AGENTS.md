# Working on this repository (for coding agents and people alike)

An avatar library for Xbox 360 homebrew on [xsdk](https://github.com/xenosworks/xsdk): C17, a public C API in
`include/xsdk/avatar.h`, three sample titles. Read in this order:

1. `include/xsdk/avatar.h`: the whole API, every function documented (units, results, ownership, when to call).
2. `samples/hello_avatar_world/main.c`: the smallest complete title.
3. `samples/store/`: everything else in use (NPCs, emotes, bands, recording, screenshots).
4. `docs/FORMATS.md` only when working on the file formats, `EVIDENCE.md` for where a fact came from.

## The layers

- **Asset layer** (`src/avatar_{pack,stfs,strb,lzx,model,texture,shape,skeleton,animation,description}.c`): decoding
  the console's files into plain structs. Builds for the PC.
- **Animation layer** (`avatar_player.c`, `avatar_own_animations.c`, `avatar_locomotion.c`, `avatar_camera.c`):
  poses, the player, the library's walk/run/jump clips (`animations/*.xaan`, embedded), locomotion, cameras. PC too.
- **Avatar layer** (`avatar.c`, `avatar_outfit.c`, `avatar_render.c`, `avatar_content.c`, `avatar_export.c`): loading
  from the console, drawing through xsdk's GPU layer, the rig export. Console only, except outfit and export, which
  the PC checks run with the file reads replaced.
- Shared internals: `avatar_internal.h` (byte reader, bit fields, allocation), `avatar_runtime.h`, `avatar_math.h`.

## Building and checking

- `./build.sh <xsdk checkout>`: the library and samples for the console. `./build.sh pc <xsdk checkout>`: the PC
  checks in `build/pc/` (README.md lists them). In WSL; Python is in WSL only.
- The PC checks need the user's own console files (the stock pack, descriptions, item packages), which are never in
  the repository: ask the user where they are.
- **What only the console shows**: drawing, the GPU, the console's file reads, how anything looks. When a change
  touches those, build for the console, say what to look at, and ask the user for a console run. Don't claim a
  rendering change works from a clean build.
- A change to a decoder should leave every PC check's output the same unless it means to change it; compare before
  and after.

## Rules

- **No Microsoft code or material**: nothing from the XDK (its avatar library, headers, docs, templates). Every
  format fact goes into `docs/FORMATS.md` with its evidence, and a new source into `EVIDENCE.md`.
- **Nothing from the console in the repository**: no packs, packages, descriptions, decoded assets, renders, rig
  exports or logs. `references/` and `test_runs/` are git-ignored for that.
- **C17** (`-std=gnu17`) for everything that builds for the console; public names start with `xsdk_avatar_` /
  `XsdkAvatar`; errors are `XSDK_AVATAR_ERROR_*` values, running out of memory included (never abort); every heap
  allocation goes through `avatar_malloc` and kin. Lines up to 120 columns, tabs, xsdk's style.
- Every public function documented in `avatar.h` in the same shape: what, parameters with units, result, ownership,
  when it may be called.
- The library draws into the title's pass and changes only the GPU state `avatar.h` lists; it never sets targets,
  clears, resolves, presents or waits.

## Pitfalls met before

- **eDRAM bands**: a target drawn in bands must place every band's colour after a whole band's depth
  (`xsdk_gpu_set_target_samples_at`), or the last, shorter band's clear hides distant geometry.
- **Animations from Blender**: legs baked from an IK rig twist the thighs unseen (run `tools/blender_legs.py`'s
  `hinge_fix` and `walk_check`'s twist check); imported rigs mix Euler and quaternion rotation modes; mind the rig's
  axis signs before keying: + about x leans the body forward and swings an arm back, the elbows bend - (left) and
  + (right) about y, the toes point down (the heads of `tools/blender_life.py` and `blender_detail.py` give the rest).
- **Avatars free only after a fence** past the last frame that drew them; the same for the locomotion's clips.
- **Hair under a hat** is a different stock record (the pack's link), not the same hair hidden.
