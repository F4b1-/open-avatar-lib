# store

The Avatar Thrift Store: walk your avatar round a small shop full of NPCs, and emote from a wheel. It grew out of
the room sample and is laid out after the user's concept picture of the store, in simple shapes and colours of our
own (no art from anywhere else): a brick back wall, board walls and a plank floor (patterns worked out in the
shader); shelves with caps, folded shirts, game cases and shoe boxes; two clothing racks and a rail of hoodies; a
fitting room; the counter with two old televisions under a neon "AVATAR THRIFT STORE" sign; cardboard boxes,
plants, track lights, and "FREE" and "0 MSP" price tags. A window in the left wall looks out on a snowy evening: snow
on the ground, pines and a fence, an evening sky, and snow falling in three layers (built once; each layer's block of
flakes is moved down as one and wrapped round, `mesh_draw_placed`).

| Control | Action |
|---|---|
| left stick | walk, relative to the camera |
| right trigger held | run instead of walking |
| A | jump (the library's, in place) |
| right stick | the camera round the avatar, and up and down |
| LB held | the emote wheel: the right stick picks, letting go of LB plays it |
| right stick click | 2 / 4 / 1 samples a pixel (2 after starting: nine avatars are drawn once per band) |
| both triggers | screenshot: a PNG in `Screenshots` next to the title |
| BACK | exit |

The wheel's emotes: wave, clap, cheer, laugh, talk, angry, sad, confused, shocked, yawn. Each plays its stock
animation (the male or female one for the emotions, by the avatar's body) with a face to match, back to standing and
a neutral face after. Talk keeps standing and moves the mouth for a few seconds, and the nearest NPC answers.

## The NPCs

Stock avatars by part name (`xsdk_avatar_load_stock`), with faces (eyes, eyebrows, mouth, facial hair): the
shopkeeper behind the counter; two shoppers chatting, taking turns to talk, the listener now and then laughing;
three browsing at the shelves and racks; two faceless white mannequins showing outfits, standing still
(`xsdk_avatar_draw_pose`). Description files in an `Avatars` folder beside the title join as more browsers (up to
three). They blink, idle now and then (the stock idles), turn to the player when it comes near and wave.

## Files

| File | What it does |
|---|---|
| `main.c` | loading, the controls, the game loop; walking, running and jumping through the library's locomotion (`xsdk_avatar_locomotion_drive`) |
| `follow_camera.c` | the camera orbiting the avatar, kept inside the room, and the stick's direction as it sees it |
| `bands.c` | the frame drawn in bands that fit eDRAM, each resolved (and into a screenshot) |
| `emotes.c` | the wheel's emotes: stock animations with faces, talking, and your own animations |
| `scene.c` | the store: its parts, the sign, what avatars bump into |
| `npcs.c` | the NPCs: outfits, places and behaviour |
| `wheel.c` | the emote wheel, drawn over the screen |
| `mesh.c` | the store's own drawing: triangles in GPU memory, lit as the avatars are, glowing, blended or overlaid |
| `font.c` | a small stroke font of our own (capitals, 0, space) for the sign, the tags and the wheel |
| `store_shaders.py` | the store's shaders, made with the library's generator, lit by its `LIGHTING` |

Screenshots use `samples/viewer/screenshot.c`. Build with `./build.sh` at the top of the project:
`build/console/samples/store/store.xex`; its log is `store.log` next to it.

## Animations of your own

Every `.xaan` file in an `Animations` folder next to `store.xex` (up to four) joins the emote wheel after the stock
emotes, named after its file (capital letters, up to nine). Picking it plays it once, then the avatar stands again.
Its root motion is moved onto your avatar's skeleton (`xsdk_avatar_animation_load` with the skeleton), as is right
for animations made in Blender on a 360sona rig (SPECS.md, section 4.8).

From Blender to the console:

1. Export your avatar from 360sona as `.glb`: T-pose, rig and bones, no props.
2. In Blender, File > Import > glTF 2.0, animate the armature (rotations; move only the hips), and export as glTF
   Binary (`.glb`) with the animation.
3. In WSL: `python3 tools/anim_convert.py my_dance.glb my_dance.xaan` (`--list` shows the file's animations,
   `--animation NAME` picks one).
4. Copy `my_dance.xaan` into `Animations` next to `store.xex`. `store.log` says whether it loaded.
