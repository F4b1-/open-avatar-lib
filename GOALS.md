# Goals

What this library is for and who it serves. SPECS.md holds the format facts, the open questions and the milestones;
this file says what we are building them towards. Where the two disagree, this file sets the direction and SPECS.md
the details.

## 1. The purpose

An avatar library for Xbox 360 homebrew built on xsdk: a developer making a small or medium-sized game or app adds the
player's own avatar, dressed and animated, with a few calls and without having to learn the asset formats behind
it.

A developer should be able to:

- show the signed-in player's avatar in a menu, a lobby or a game scene in an afternoon;
- make it move: idle, wave, celebrate and the other gestures with the stock animations every console already has,
  and walk, run, jump and sit with the library's own (the stock set has no locomotion);
- put several avatars in one scene: the local players, and NPCs the game ships as descriptions;
- fit it into their own renderer, camera, lighting and frame loop, rather than the other way round;
- give it motion of their own: animations made in a 3D program, or bones posed from the game's code.

## 2. Who it is for

Small and medium-sized homebrew games and applications: one developer or a small team who knows C or C++ and xsdk
but not the avatar formats, the Xenos GPU's details or skeletal animation. The library takes that knowledge off
their hands.

The code itself is written to the standard of a **very ambitious hobby project**. It is cleaner, better tested and
better documented than most homebrew, and it doesn't go as far as an engine's machinery. Concretely:

- a clear module layout a newcomer can find their way around in (section 6);
- every format fact backed by evidence (SPECS.md, section 2), every public function documented in its header;
- tests that run on the PC against the developer's own console files, plus sample titles that run on the console;
- no clever abstraction that a reader has to unpick: plain C with plain structs, one obvious way to do each thing.

## 3. Design principles

1. **Easy things are one call.** Loading the player's avatar, drawing it and playing an animation each take one call
   with sensible defaults. Nothing has to be configured before the first picture.
2. **Hard things stay possible.** A developer who wants more can reach the next layer down: poses, the skeleton and
   animations (the animation layer), then the decoded meshes, textures and asset data (the asset layer), all public,
   documented structs.
3. **The game stays in charge.** The library draws into the game's own pass and command stream. It never owns the
   frame, the render targets, the camera or the main loop, and it documents exactly which GPU state it changes
   (SPECS.md, section 1.3).
4. **Honest errors.** Every call that can fail returns a result code the developer can act on. A missing marketplace
   item falls back to a stock part instead of failing the whole avatar, and says so.
5. **Nothing bundled, nothing downloaded.** Every asset is read at run time from the console the game runs on. The
   library ships code only, and so do its tools: anything they make from Microsoft's assets (a rig for a 3D
   program, say) is made on the developer's own machine from their own console files and stays there. Games built
   on it follow the same rule: they use the console's meshes, textures and stock animations, read at run time, and
   never carry copies. A game ships avatars as descriptions (item ids and colours, no art), plus its own content.
6. **Clean-room and MIT.** No Microsoft code, XDK material or decrypted system code. It is a companion library to
   xsdk for now (SPECS.md, section 5.4), laid out so that it could still move into xsdk as a copy.

## 4. Features

### 4.1 Expected by every developer (the first release)

| Feature | What the developer gets |
|---|---|
| Load an avatar | from a signed-in player, from a saved 1000-byte description (made on the console, or designed in a web editor such as [360sona](https://360sona.com/), which exports one), or built from stock parts by name |
| Loading state | each avatar reports loading, ready or failed, so a game can show something while it loads; loading may be synchronous at first, and becomes asynchronous (4.2) without changing the interface |
| Draw it | textured and coloured as on the dashboard, in the game's own pass, with the game's view and projection |
| Light it | a simple light the game sets: direction, colour and ambient colour, with defaults close to the dashboard's look |
| Animate it | the console's stock animations by name, or by a constant for the common ones, and a list of those available (names and lengths); play now, queue the next, a cross-fade between them, speed, looping or one-shot, and a signal when a one-shot ends |
| Pose it | an animation player produces a pose, and drawing takes an avatar and a pose. The simple path hides this (each avatar has its own player); a game that wants to can pose bones itself, share one player between avatars, or mix the two |
| Place it | a world transform per avatar; several avatars per scene |
| NPCs | avatars the game ships as description files (designed on a console or in 360sona) and loads like the player's; an item the console lacks is replaced by a stock part of the same kind, and the game is told |
| Face | the eye and mouth expressions the textures carry; blinking on its own; talking driven by a 0-1 level the game sends (from voice or a dialogue line); and the faces the stock animations carry, if their face tracks can be decoded (SPECS.md, section 4.5, not yet known) |
| Clean up | one call frees everything, after the game's fence says the GPU is done |

### 4.2 Wanted soon after

| Feature | What the developer gets |
|---|---|
| Custom animations | animations made in a 3D program, played like stock ones (section 4.5) |
| The library's own animations | walk, run, jump and sit, which the stock set lacks: made through section 4.5, shipped with the library under MIT, free for games to ship and edit |
| Layering | one animation over part of the body (wave while walking), with upper- and lower-body bone masks ready-made; and additive layers, a game's own adjustment on top of another animation (a stock one, say) |
| Look-at | the head and neck turned towards a point, on top of whatever animation plays |
| Attachments | a bone's transform (hands, head), so the game can put a prop in a hand or a hat on a head |
| Random avatars | a random, valid stock avatar, optionally of a given body type, for NPCs and crowds |
| Bounds | a bounding box per frame, for culling, cameras and simple picking |
| Body shape | the description's height and weight |
| Asynchronous loading | load on a worker thread and poll the loading state, so a game doesn't stall while an avatar loads |

### 4.3 Later, if there is demand

- Avatar props: the carried items from the marketplace and awards (component mask 0x1000), drawn on their hand bone.
  Some bring their own skeleton and animation. Left out until then (decided 2026-11-04, SPECS.md section 6.1); a
  game can still attach its own props to a bone (4.2).
- Blending more than two animations at once.
- Face keys in custom animations (custom properties in the 3D program's export).
- Rendering to a texture for menus and thumbnails (a helper over xsdk's render targets).
- Level of detail for crowds.
- A small posing sample on the console (bones set with the gamepad), as a demonstration rather than a tool.

### 4.4 Out of scope

- An avatar editor, or writing descriptions back to profiles: web editors such as 360sona exist, and the library
  loads what they export.
- Downloading items from Microsoft's servers.
- Physics, ragdolls, inverse kinematics, spring bones or a scene graph: a game builds those on the pose and bone data
  the library exposes.
- Distributing Microsoft's assets in any form: meshes, textures and stock animations, in the library, its tools or
  its samples, and in exported files such as glTF rigs.
- Platforms other than the Xbox 360 (the parsers build on the PC for tests and tools, not as a PC renderer).

### 4.5 Making new animations

A game's own animations are made in a 3D program that reads and writes glTF (Blender, and most others), and reach
the console through a PC tool:

1. **A rig to animate.** A glTF file with the avatar's 71-bone skeleton, readable bone names and a body to look at,
   in the T-pose. Two sources:
   - a 360sona export (rigged, with numbered or humanoid bone names), if its rig matches the library's skeleton;
     to be checked against SPECS.md, section 4.4, before anything relies on it;
   - our own rig exporter, a PC tool that builds the file from the developer's own console files (the stock pack),
     so the workflow doesn't depend on a website. Planned as the fallback, and the first choice if 360sona's rig
     doesn't match.
2. **Animate** in the 3D program as usual, and export glTF.
3. **Convert**: a PC tool reads the glTF, matches its bones to the library's by name or number, resamples the motion
   and writes the library's own animation format.
4. **Play**: the game loads the file from its own data and plays it through the same player as stock animations,
   with the same cross-fades, queueing and layering.

Decided for now (2026-11-06), both open to change:

- **The library's own animation format**, small and documented, rather than the console's animation block, whose
  last sections aren't decoded (SPECS.md, section 4.5). Writing the console's format could come later.
- **cgltf** (single-file, MIT) for reading glTF in the PC converter: the project's first third-party code, recorded
  in THIRD_PARTY_NOTICES.md, used only by the PC tool and never in the console library.

Animations made from scratch on such a rig belong to their author and can ship with a game. Stock animations are
always available to a game, played from the console's own files; what a game can't do is carry a copy of one,
edited or not, in its own data (principle 5). To vary a stock animation, a game ships only its own adjustment and
layers it over the stock one at run time (4.2, additive layers).

## 5. The interface

Three layers, all public:

- **The avatar layer** (`xsdk_avatar_*` on an `XsdkAvatar`): what nearly every game uses. Load, play, update,
  draw, close; light, face and look-at helpers. A C++ wrapper (`xsdk::Avatar`) on top, header-only and with no
  behaviour of its own.
- **The animation layer**: what ambitious games and tools use. The skeleton; animations, stock or custom, and the
  list of stock ones; sampling an animation at a time into a pose; blending two poses with a weight and a bone mask;
  a pose's skin matrices and bone transforms; drawing an avatar with any pose. Its first parts exist since milestone
  6 (`xsdk_avatar_skeleton_build`, `xsdk_avatar_skin_matrices`).
- **The asset layer** (the pack, descriptions, models, textures, skeleton blocks, animation blocks): plain structs,
  decoded once, no GPU involved. It exists for everything but animations.

The avatar layer should read like this (names to be settled; SPECS.md, section 1):

```c
XsdkAvatar *avatar;
xsdk_avatar_load_profile(&avatar, user_index);   // or _load_description, _load_stock
// while xsdk_avatar_state(avatar) == XSDK_AVATAR_LOADING: show something else
xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ONCE);   // after the current one, cross-faded

// each frame, inside the game's own pass:
xsdk_avatar_update(avatar, dt);                  // its own player, blinking, talking
xsdk_avatar_set_transform(avatar, &world);
xsdk_avatar_draw(avatar, &view_projection);

xsdk_avatar_close(avatar);                       // after the game's fence
```

And one layer down, a game that poses the avatar itself:

```c
XsdkAvatarPose pose;
xsdk_avatar_sample(animation, time, &pose);              // or the game's own bones
xsdk_avatar_blend(&pose, &wave_pose, 1.0f, XSDK_AVATAR_MASK_UPPER_BODY, &pose);
xsdk_avatar_draw_pose(avatar, &pose, &view_projection);
```

The test of the interface: a developer who has never seen the library gets an animated avatar on screen from
the README and one sample, without opening SPECS.md.

## 6. Code layout

Laid out as xsdk is, so the library could move into xsdk as a copy (SPECS.md, section 1.3):

```
include/xsdk/avatar.h      the C interface: the avatar layer, the animation layer, the asset layer
include/xsdk/avatar.hpp    the C++ wrapper
src/avatar_*.c             one file per concern: pack, STFS, LZX, model, texture, skeleton, animation, draw
tests/                     PC tests and tools, run against the developer's own console files
tools/                     console probes, the shader generator, and the PC animation tools (glTF converter, rig
                           exporter)
third_party/               cgltf, for the PC animation tools only
samples/                   small titles that show one thing each: show an avatar, animate it, several avatars,
                           a custom animation
research/                  the Python prototypes behind SPECS.md (not part of the library)
docs/ or README.md         getting started, the interface, the GPU state the library touches, making animations
```

Rules that keep it readable: the asset layer never touches the GPU; only the draw code does. Every source file
starts with what it does and where its format is described in SPECS.md. Every format fact, including those taken
from public reverse engineering, is cited in SPECS.md.

## 7. When it is done

The first release is done when:

1. the features of section 4.1 work on the console with the developer's own avatar and with stock-only avatars;
2. the samples build and run, and a reader can follow each in a few minutes;
3. the README gets a newcomer from nothing to an animated avatar;
4. the PC tests pass on the user's console files;
5. it builds as a companion library beside xsdk, laid out so that a later move into xsdk stays a copy.

## 8. Where we are (2026-11-06, evening)

- Done: reading the stock pack, item packages and the description; decoding meshes, textures and materials (C, tested
  against the prototypes on the whole pack); the textured, coloured avatar on the console (milestone 5); the
  avatar's skeleton, derived from the data, and skinning on the GPU (milestone 6); stock animations played,
  looped and cross-faded on the console (milestone 7).
- Built, waiting for its console run (milestone 8): the avatar layer of section 5 (load from a profile, a
  description or stock parts; play, queue, update, set_transform, draw, draw_pose, info, close), the player in the
  animation layer, the stock fallback for missing items, the C++ wrapper, a README and two samples (`hello`, and
  `viewer` with several avatars, NPCs from description files, recorded drawing and cost measurements).
- Built, waiting for the same console run (milestone 9, first part): the face. The expression frames worked out,
  set per eye, eyebrow and mouth; blinking on its own; talking from a 0-1 level.
- Built, waiting for its console run: a walk made in code (`xsdk_avatar_animation_walk`), standing in for the
  library's own until milestone 11, and a first small game, `samples/store`: walk your avatar round the Avatar Thrift Store
  among NPCs and emote from a wheel (stock avatars can now have faces: `xsdk_avatar_load_stock` takes face features).
- Next: the faces the stock animations carry, if they can be decoded (the rest of milestone 9), then custom
  animations (milestone 10) and the library's own walk, run, jump and sit (milestone 11). SPECS.md, section 6.2.
  A first check for 4.5: compare a 360sona export's rig with the library's skeleton.
- Built, waiting for its console run (2026-11-06, the user asked for better image quality): per-pixel lighting with a
  light the title sets (direction, colour, sky and ground ambient, highlight), and anti-aliasing in the viewer
  (2 or 4 samples a pixel, the screen drawn in bands).
- Not yet in the avatar layer, from 4.1: the loading state (loads are synchronous), and the stock animations'
  lengths in the list (names only).
- Known limits today: every avatar has the same height; a missing accessory is left out, not replaced; where a
  console without a hard drive keeps the stock pack is not yet known (SPECS.md, section 5.1).
