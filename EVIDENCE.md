# Evidence

Where the library's facts come from. Every fact in [docs/FORMATS.md](docs/FORMATS.md) cites its evidence inline,
and the code's comments cite FORMATS.md; this file says what each named source is, so that a citation such as
"test run 6" or "sample C1" can be looked up. A fact that rests on a source is found by searching FORMATS.md and the
code for the source's name.

None of the files below is in the repository, nor any output made from them: they are a console's own files, or a
player's. They are named here by kind and identified by hash (SHA-256, the first 16 hex digits), so that anyone with
the same files can check the same facts.

## The console's own files

| Name | What | Identified by |
|---|---|---|
| **Pack** | The stock asset pack: package `FFFE07DF00000002` from the hard drive's system extended partition (`Content\0000000000000000\FFFE07DF\00008000\`), holding `AvatarAssetPack.toc` (version 2, 1282 records). The source of every stock asset, the skeleton, the 60 stock animations. Copied off the console by `tools/probe_pack`. | `9dcef5b4cc309eaf` (another console's pack of the same version may differ in its signature) |
| **SysExt** | A copy of the console's system extended partition, 2026-11-03: the pack above and the files beside it (FORMATS.md 4.3). | the pack's hash |
| **System** | A copy of the console's flash system partition, 2026-11-03: xam.xex, dash.xex and 34 other files; it holds no avatar assets (FORMATS.md 4.2). Not disassembled. | - |

## Samples

| Name | What | Identified by |
|---|---|---|
| **Store samples** | 10 marketplace item packages from Avatar Thrift Store's `samples/` (MIT repository, its own test files): five items, a male and a female version each: "Ducktales Logo Tee", "Renaissance Hat 1" and "2", "Renaissance Mask 1" and "2". The first evidence for the STRB container, the model block and the metadata (FORMATS.md 3.1-3.3). | `9e6dac9b32bef925`, `ad691cf40d9bae38`, `8af2c4db418acb21`, `4f011adcc988e57c`, `7b25638ded359adb`, `d356be2c4921dc2f`, `1748d7a6b3e8adc4`, `03e4076f6562e582`, `2daa46e088363e97`, `c0d7ca97392e640f` |
| **P1** | A signed-in profile's avatar description, 1000 bytes, read on the console 2026-11-03 with `tools/probe_manifest` (FORMATS.md 4.6): a male body, a costume (C1), sunglasses (G1) and a carried prop over stock clothes and hair ("Raised Front"). The PC checks' description. | `f10edbefe6dfa6c2` |
| **P2** | The same profile's description on 2026-11-08: a suit and a helmet (test runs 7 and 8). | not kept |
| **P3** | The same profile on 2026-11-11: a Santa hat with mouse ears over "Raised Front" (test run 9). | not kept |
| **C1** | P1's costume, a marketplace package filling shirt, trousers, shoes, gloves, wrist and ring slots (0xAB8): five parts, a colour table of one pure red, green and blue entry, a shape block hiding 2218 of MaleBody's triangles. | `47494a6e77a413b8` |
| **G1** | P1's sunglasses, a marketplace package (slot 0x100): a frame part with a "CREATURE" print and a see-through green lens part with a yellow-striped reflection map (FORMATS.md 3.6). | `daa97acef92f1476` |
| **Prop1** | P1's carried prop (slot 0x1000), a marketplace launcher with its own skeleton (block 5) and animation (block 1). | `8427aa8ac2d5a812` |
| **R1** | A 360sona export (glTF 2.0, generator "360sona") of P1's avatar, 2026-11-06: 360sona's rig and its bone names (FORMATS.md 4.7). | local file, not kept by hash |

## Console runs

Runs with a number kept their files (logs, screenshots, exports) in a local `test_runs/<n>` folder; the others were
looked at on the console and reported. Each new console run adds an entry here.

| Name | Date | What ran | What it showed |
|---|---|---|---|
| **Test run 1** | 2026-11-04 | the viewer (milestone 5's first textured build) on P1 and two stock outfits | textures and the face drawn right; skin through tight clothes; C1 black: its mask tinted by the description's empty colours (FORMATS.md 3.6) |
| **Test run 2** | 2026-11-04 | the viewer with hiding (FORMATS.md 3.7) | clean on all three outfits; C1's colour table then settled the empty-colour rule |
| **Test run 3** | 2026-11-04 | the viewer with face shapes and clamped face layers, props left out, beside the system's own render of P1 (an Aurora screenshot) | proportions agree with the system's (the first evidence for the position scale, FORMATS.md 3.3); G1's lenses wrong, which found that models are a series of parts (3.6); the light flatter and darker than the system's (5.3) |
| **Console run 2026-11-05** | 2026-11-05 | the viewer with parts drawn each with its own material | G1 right: milestone 5 done |
| **Test run 4** | 2026-11-06 | the viewer skinning on the GPU, a hand-set pose (milestone 6) | skeletons built for all three outfits; no tearing at shoulders, elbows, knees or neck; the Head, hair and sunglasses follow bone 19 (FORMATS.md 4.4) |
| **Console run 2026-11-06a** | 2026-11-06 | the viewer playing stock animations (milestone 7) | Stand loops; Wave and Clap play and cross-fade both ways (FORMATS.md 4.5) |
| **Test run 5** | 2026-11-06 | the avatar layer and the face (milestones 8, 9) | all avatars load; faces, expressions and blinking work; costs: CPU 107 us and GPU 339 us an avatar a frame; skin through C1 again (an item package's record left unset), fixed the same day |
| **Console run 2026-11-06b** | 2026-11-06 | the store sample | C1 right again; standing, walking, the emote wheel's and the NPCs' animations work: milestones 8 and 9 done |
| **Test run 6** | 2026-11-08 | the store sample with an animation of our own (`Wave.xaan`) | the Blender animation plays (milestone 10); a dark slab in the far corners from screen row 208: the last band's colour where the other bands keep depth (fixed: every band's colour after a whole band's depth) |
| **Test run 7** | 2026-11-08 | `samples/rig_export` on P2 and the bare bodies | all three rigs written; the bare bodies equal the PC's exports to the last bit of some floats (the console's fused multiply-adds); P2's rig passes the Blender round trip (FORMATS.md 4.9) |
| **Test run 8** | 2026-11-08 | `samples/rig_export` with baked textures | every file and PNG checks out; P2's suit and helmet in their own colours in Blender (FORMATS.md 4.9) |
| **Console run 2026-11-09** | 2026-11-09 | the store with the band layout fixed and a matte light | the far corners clean, the avatars matte like the system's Dress Up screen (FORMATS.md 5.3) |
| **Console run 2026-11-10** | 2026-11-10 | the store with the library's walk, run and jump; `samples/hello` (C++) | all three play as made; milestone 11 done |
| **Console run 2026-11-11a** | 2026-11-11 | the refactored library: the renderer, content and outfit code, the camera helpers, the store split | everything as before the refactoring |
| **Test run 9** | 2026-11-11 | the store on P3 | "Raised Front" through the Santa hat: hair under a hat not swapped for its hat variant (FORMATS.md 3.7) |
| **Console run 2026-11-11b** | 2026-11-11 | the store with hair under a hat swapped | the hat variant ("Quiff (Hat)") stays under the hat (FORMATS.md 3.7) |
| **Test run 10** | 2026-11-11 | hello_avatar_world, and the store after milestone 12 (the library's locomotion, RB recording, GPU time), on P3 | hello_avatar_world walks, runs and jumps on its stage with its shadow; the store's locomotion loads (walk 0.877, run 1.957 m/s, jump); the hat variant again; RB records the avatars' draws (35,122 dwords a frame) and runs them in both bands, the avatars looking the same as drawn directly; the whole frame 10.6-12.8 ms of GPU time and 1.1-1.3 ms of CPU at 2 samples with nine avatars |

## Public sources

Read for facts only; nothing copied from them into the library.

| Name | What | Licence | Read |
|---|---|---|---|
| **Velocity** | https://github.com/hetelek/Velocity, `XboxInternals/AvatarAsset/` | GPL-3.0: read only, never copied | the STRB container, metadata, colour table, block ids, subcategories (FORMATS.md 3.1) |
| **All-Stars Avatar** | https://github.com/Demaunix/All-Stars-Avatar, `src/avatar/tools/` | MIT | `avatar_model.py` (the model data layout, 3.3), `avatar_desc.py` (the description, 4.6), `avatar_colours.py` (the colour rule and face layers, 3.5-3.6); `avatar_texture.py` not read |
| **360sona** | https://360sona.com | web editor | renders avatars in WebGL; reads and writes the 1000-byte description (4.6); its rigged export (R1, 4.7) |
| **XNA reference** | XNA Game Studio 4.1, archived on learn.microsoft.com: "AvatarBone Enumeration" (dd940212), "AvatarEye", "AvatarEyebrow", "AvatarMouth Enumeration" (dd940224, dd940223, dd940225) | public documentation | bone names (4.4) and the face's expression names (3.5): names only, no code or material |
| **Avatar Thrift Store** | its `core/` (STFS, GPD) and `docs/STATUS.md` | MIT | STFS block and hash table addressing, the asset id, the gender byte (3.1, 4.3) |
| **xsdk** | https://github.com/xenosworks/xsdk (README, SCOPE.md, CODE_GUIDELINES.md, docs/HARDWARE.md) | MIT | the GPU's eDRAM layout, the clock, the conventions the library follows |
| **libmspack** | `lzxd.c` | LGPL: not used | documents the same LZX algorithm the library implements from its public description (3.2) |

## Research prototypes

`research/` holds the Python prototypes the formats were worked out with (our own code; see
[research/README.md](research/README.md)). The C library is checked against them on the pack: `avatar_dump` with
`research/check_c.py` (models, skeletons, animations: 0 differences).
