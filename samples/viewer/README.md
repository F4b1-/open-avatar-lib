# viewer

The library's proving ground, now a sample of the avatar layer (milestone 8; SPECS.md, section 6): several avatars
in one scene, each with its own animation, drawn directly or recorded into a command buffer, with what they cost in
the log.

- **Your avatar**: the first signed-in profile's (`xsdk_avatar_load_profile`). Items the console doesn't have are
  replaced by stock parts of the same kind; the log says how many.
- **NPCs**: every 1000-byte `.bin` description in an `Avatars` folder next to `viewer.xex`
  (`xsdk_avatar_load_description`): saved from a console, or exported from a web editor such as 360sona. Up to six.
- **Two stock avatars** by part name (`xsdk_avatar_load_stock`): a male in "Punk Jacket and Tee" and "Black Skinny
  Fit Jeans", a female in "Hacking Jacket" and "Denim Skirt".

They stand side by side, 0.9 m apart, each looping "Animation Generic Stand 0" in its own rhythm and blinking on its
own timer (milestone 9). The light is fixed in the world (from the upper left, in front), so turning the camera
shows their sides lit differently.

| Control | Action |
|---|---|
| d-pad left/right | which avatar the camera looks at |
| right stick | turn around it |
| left stick up/down | zoom |
| Y | it waves once, then stands again (cross-faded, queued) |
| B | it claps once, then stands again |
| A | everyone celebrates once |
| X | animations / hand-set pose / bind pose (the last two through `xsdk_avatar_draw_pose`) |
| d-pad up/down | its face: neutral, sad, angry, confused, laughing, shocked, happy, yawning, sleeping, looking to its left, to its right, up, down, one eyebrow raised (`xsdk_avatar_set_face`) |
| LB held | it talks (`xsdk_avatar_set_talking`, the level rising and falling like syllables) |
| START | blinking off / on for everyone (`xsdk_avatar_set_blinking`; on after loading) |
| RB | drawn directly / recorded into a command buffer each frame, then run once per band |
| right stick click | 4 / 1 / 2 samples a pixel: anti-aliasing (4 after starting) |
| both triggers | screenshot: a PNG in `Screenshots` next to the viewer (`AvatarViewer_001.png`, ...) |
| BACK | exit |

## The log

`viewer.log` next to the title:

- per avatar as it loads: parts, draws, items replaced and left out, GPU and CPU memory, load time;
- every five seconds: the samples and bands, the CPU time of all avatars' updates and draws (or recording), the GPU
  time between clock words written around the frame's bands (their draws and resolves), the command stream's dwords
  a frame, and the recorded buffer's size.

## Files

| File | What it does |
|---|---|
| `main.c` | loading, the camera, the controls, the hand-set pose, the frame loop, the measurements |
| `screenshot.c` | the screen as a PNG (stored deflate, no library: about 2.8 MB at 720p) |

## Building

`./build.sh` at the top of the project (README.md) builds it into `build/console/samples/viewer/viewer.xex`. Copy it to
the console (with an `Avatars` folder if you like), launch it from Aurora, and send back `viewer.log`.

## Anti-aliasing in bands

A 720p target with 2 or 4 samples a pixel and its depth doesn't fit the 10 MB of eDRAM, so the viewer draws the
screen in bands of whole 32-row strips, as many as fit: one at 1 sample, two at 2, three at 4 (`band_rows`). For
each band (`draw_bands`) it sets a target of that height with the samples, moves the viewport up by the rows above
so that the camera's projection still covers the whole screen, draws the avatars (or runs the frame's recording
again), and resolves the band, its samples averaged, into its rows of the front buffer. The library needs nothing
for this: its draws go into whatever target is set, and its lighting reads the camera from the view-projection,
which stays the same in every band. A title does the same with its own scene.
