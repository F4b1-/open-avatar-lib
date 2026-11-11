# Getting started

From nothing to your own avatar walking on the console: the tools, a first build, `hello_avatar_world` on the
console, and a title of your own. About an hour, most of it the toolchain's build.

## What you need

- **An Xbox 360 that runs homebrew** (RGH or JTAG), with a way to launch a `.xex` (a dashboard such as Aurora) and to
  copy files to it (FTP). xsdk titles run on the retail kernel and dashboard.
- **A signed-in profile with an avatar** on that console, for your own avatar; without one the samples show a stock
  avatar.
- **A Windows PC with WSL Debian.** Everything builds inside WSL; the shader compiler's glslang is a Windows program
  that WSL runs.
- In WSL: `git`, `make`, `cmake`, `ninja-build`, `python3` and the packages xsdk's toolchain build asks for.

## 1. xsdk and its toolchain

The library builds with [xsdk](https://github.com/xenosworks/xsdk), which brings its own GCC for the console.

```
git clone --recursive https://github.com/xenosworks/xsdk
cd xsdk
tools/toolchain/build.sh        # GCC 15, binutils and newlib for powerpc-xsdk-elf, into ~/xsdk-gcc (a while)
make                            # build/libxsdk.a, build/host/libxsc.so (the shader compiler) and xsdk's test title
```

`--recursive` checks out xost, the shader toolchain, in `xenos-toolchain/`. It needs glslang, which is not tracked:
put the Windows release of glslang in `xenos-toolchain/tools/glslang` (xost's README). xsdk's docs name glslang
16.3.0; the library was built with 16.6.0, as 16.3.0 has no prebuilt release.

The library was last tested with xsdk at commit `01c7a43`. A newer xsdk should work; if it doesn't, check out that
commit (`git checkout 01c7a43 && git submodule update`) and open an issue.

## 2. Build the library and the samples

```
git clone https://github.com/F4b1-/open-avatar-lib xbox360_avatar_lib
cd xbox360_avatar_lib
./build.sh <path to xsdk>
```

That builds `libxsdk_avatar.a` and the three samples into `build/console/samples/`: `hello_avatar_world`, `store`
and `rig_export`, each a `.xex`. `./build.sh` without a path tells you it needs one.

## 3. hello_avatar_world on the console

1. Copy `build/console/samples/hello_avatar_world/hello_avatar_world.xex` to the console (a folder of its own, say
   `Hdd1:\Apps\hello_avatar_world\`).
2. Sign in with your profile, then launch it from the dashboard.
3. **What you should see:** your avatar on a plain grey-blue screen, with a soft shadow at its feet. The left stick
   walks it where you point (up is away from the camera), the right trigger held makes it run, A makes it jump.
   It stays within 3 m of the middle. BACK exits.
4. The title writes `hello_avatar_world.log` beside the `.xex`. Copy it back and read it: it says what loaded (parts,
   items the console lacked and what stood in for them, memory used) and how fast the walk and run are for your
   avatar's body.

**If it goes wrong:**

| What you see | What it means |
|---|---|
| a stock avatar, not yours | no profile was signed in when it started, or its description couldn't be read: the log says which |
| a part missing, or a plain stand-in | the item isn't on this console (a marketplace item never downloaded): the log names it |
| it exits at once | the log's last lines say why: usually the stock pack couldn't be opened, or the GPU refused a shader |
| no log at all | the title didn't start: check it is a homebrew-capable console and the `.xex` copied whole |

Xenia (Canary) runs xsdk titles for a quick crash check; what avatars look like, the console decides.

## 4. A title of your own

Start from `samples/hello_avatar_world/main.c`: about 140 lines, all of them the library's calls or what every xsdk
title does. A CMake project for it, beside the library:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_title C ASM)

add_subdirectory(../xbox360_avatar_lib xsdk_avatar)   # the library, built with your xsdk

add_executable(my_title main.c)
target_compile_options(my_title PRIVATE -std=gnu17 -O2 -Wall -Wextra)
target_link_libraries(my_title PRIVATE xsdk_avatar m)
xsdk_add_xex(my_title)
```

Configure it with your xsdk's toolchain file, which also tells the library where xsdk is:

```
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<path to xsdk>/cmake/xsdk.cmake
cmake --build build
```

Then read `include/xsdk/avatar.h`: every call says what it does, in which units, what it returns, and when it may be
called. `samples/store` shows the rest: NPCs, faces and emotes, your own animations, anti-aliasing, screenshots.

Two rules to keep in mind from the start:

- **Close an avatar only after the GPU is done with it**: wait on a fence past the last frame that drew it, then
  `xsdk_avatar_close` (and `xsdk_avatar_locomotion_free`).
- **The library draws into your pass**: set your target and depth state first; it changes only the state
  `avatar.h` lists, and leaves blending off.

## 5. The checks on the PC (optional)

The decoders, the player, the locomotion and the clips can be checked on the PC against your own console's files.
They need the stock pack, which FTP can't copy while the system runs: run `tools/probe_pack` on the console (its
README says how), copy `FFFE07DF00000002` back, and:

```
./build.sh pc <path to xsdk>
build/pc/locomotion_check <path to FFFE07DF00000002>
build/pc/walk_check <path to FFFE07DF00000002>
```

The README lists every check. Keep the pack and anything made from it out of any repository: they are the
console's files.

## 6. Your own animations

`samples/rig_export` writes your avatar as a rig for Blender; `tools/anim_convert.py` brings the animation back. The
README's "Making animations" walks through it.
