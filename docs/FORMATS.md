# The formats

What the library knows about Xbox 360 avatar files, and how each fact was found. It reads them as the console stores
them: item packages and the system's stock pack, the assets inside them (models, textures, shapes, skeletons,
animations) and a profile's 1000-byte description. The code's comments cite this file by section ("FORMATS.md,
section 3.7").

Every fact names its evidence inline: a sample file, a console run, or a public source. [EVIDENCE.md](../EVIDENCE.md)
says what each named source is ("test run 6", "sample C1", "Velocity"). What is still unknown is in section 5.

## 1. What the library reads

- Avatar assets: the STRB container, its LZX-compressed model blocks, meshes, textures and colour tables, shape
  overrides, skeletons and animations (section 3).
- The 1000-byte avatar description: which assets, which colours, which body shape (4.6).
- Where the assets a description names are on the console: stock assets in the system's pack, marketplace and award
  items in their packages (section 4).

Not in its scope: editing avatars or writing descriptions back to profiles, downloading anything from Microsoft's
servers, and bundling any asset. Every asset is read at run time from the console the title runs on.

## 2. Rules

- **No Microsoft code or material.** Nothing from the XDK: not its avatar library, headers, docs, asset templates or
  converter. The library is written from samples, console runs and public reverse engineering.
- **Every format fact cites its evidence**: a sample file, a console run, or a public source (EVIDENCE.md). Third-party
  code is read for facts only: GPL code (Velocity) is never copied; MIT code (All-Stars Avatar) could be, with its
  notice, but the library is our own implementation.
- **No assets in the repository.** No item packages, no decoded blocks, no system files, no outputs made from them.
  The checks run on one's own files and name them by hash (EVIDENCE.md).

## 3. The asset file (`asset_v2.bin`): established

An avatar item package (STFS, LIVE) holds `asset_v2.bin` and `icon.png`. Avatar Thrift Store's `core/` already reads
the package. All facts below were checked on all 10 sample files (5 items, a male and a female version each: a tee,
two hats, two masks).

### 3.1 STRB container

Source: Velocity, `XboxInternals/AvatarAsset/AvatarAsset.cpp` (GPL-3.0, read only), checked on the samples: the
block walk ends exactly at the end of every file.

| Offset | Size | Field | Samples |
|---|---|---|---|
| 0x00 | 4 | magic `STRB` | |
| 0x04 | 1 | block alignment stored | 1 |
| 0x05 | 1 | little-endian | 0 (so block headers are big-endian) |
| 0x06 | 16 | GUID | the same in every sample |
| 0x16 | 1 | block id size | 1 |
| 0x17 | 1 | block span size | 4 |
| 0x18 | 2 | unused | |
| 0x1A | 1 | block alignment (if stored, else 1) | 4 |

Blocks start at `align(0x1E)` (0x20). Each has a header of `align(id + span + span)` bytes (12): id, data length,
field size; then the data, padded to the alignment.

| Id | Block | In the samples |
|---|---|---|
| 1 | animation | |
| 2 | texture | |
| 3 | model | all |
| 4 | shape overrides | tees |
| 5 | skeleton | |
| 6 | metadata (version 1) | |
| 7 | custom colour table | tees |
| 8 | metadata (versioned) | all |

**Metadata (id 8)**, little-endian although the container is big-endian: version (1 byte, 2 in all samples), gender
(1 byte: 1 male, 2 female, 3 both), asset type (u32: 1 component, 2 texture, 3 shape override, 4 animation, 5 shape
override post), type details (u32: the component mask, 0x08 shirt, 0x40 hat, 0x100 mask), subcategory (u32: 0xC9 tee,
0x1F8 brimmed hat, 0x2BC glasses, ...), skeleton version (1 byte: 1 NXE, 2 Natal, 3 both; 2 in all samples). Its
gender byte is the one STATUS.md of Avatar Thrift Store found at file offset 0x2D.

**Colour table (id 7)**, little-endian: u32 count, then per entry three colours of (B, G, R, A, u32 unknown), 24 bytes
an entry. Checked 2026-11-04: 36 of the pack's 39 tables have exactly that size (the C reader matches research on all
36); 3 (Male Power Tee among them) have another layout, not read yet. An entry is an item's three default custom
colours: the sport tops' and costume C1's and prop Prop1's single entry is pure red, green,
blue, so the tint becomes the mask's own colour (3.6).

### 3.2 Model block (id 3): compression

Established on the samples with our own LZX decoder (research/lzx.py); All-Stars Avatar's `avatar_model.py` notes
that its model blocks are "LZX-expanded" first.

- The block is a list of chunks: u32 compressed size, u32 output offset, u32 output size (little-endian), then the
  compressed bytes. Output sizes are at most 0x8000; the tees have two chunks (0x8000 + the rest).
- **Each chunk is an LZX stream of its own**: 16-bit little-endian words read MSB first, a **32 KB window** (15 bits;
  16 to 21 fail on every sample), its own 1-bit E8 header (always 0), and Huffman lengths and repeated offsets starting
  from zero, not carried over from the chunk before. Carrying them over decodes without an error but gives wrong
  bytes past 0x8000: that hid the body's indices (4.1) until it was fixed. Verbatim and aligned blocks occur.
- The library needs its own LZX decoder (MIT). libmspack and Xenia's copy of it are LGPL and are not used.

### 3.3 Model data (expanded)

Source: All-Stars Avatar, `src/avatar/tools/avatar_model.py` (MIT; its authors derived it from the bytes and from the
vertex declarations of a game's avatar renderer). Reproduced independently on the samples (research/mesh.py): for
every sample the meshes' total index bytes equal header word 4 and their number equals header word 5.

- A header of u32 words. Known so far: word 3 = the vertex buffer's bytes once unpacked (vertex count times a stride
  of 36 or 40, rounded up to 0x80), word 4 = the index buffer's bytes as 16-bit indices, word 5 = mesh count. Words
  0-2, 6-11 are unknown (words 2 and 7 look like a texture size: 0x3000-0x9000; 3.5 settles them). From 0x30 the
  first part's header and material follow (3.6), not header words.
- Meshes, each found by its step value 7.071068e-6 (`02 44 ED 36`), all little-endian, bit fields LSB first:
  - u32 vertex count, f32 step, f32 min x, y, z;
  - a bit stream: 3 x 6-bit position widths, then for normal, blend weights, blend indices and colour a u32 base and
    a u32 bit width;
  - per vertex: quantised x, y, z | normal | weights | bone indices | colour | texture coordinates as 16-bit halves
    (4 or 6 halves in the samples: 2 or 3 UV sets);
  - each packed element is `(base + stored) mod 2^32`; normals are 11:11:10 signed (HEND3N);
  - pad to a byte, u32 index count, u16 0, u16 index width (`bit_length(vertex count - 1)`), the indices, pad.
- Positions: `min + q * step * k`. All-Stars Avatar uses k = (2, 2/sqrt(1.5), sqrt(3)), fitted to make items meet the
  body; it calls this inferred, z least certain. Checked on the console's own pack (research/render.py, 2026-11-03):
  with k = (2, 2, 2) the head is stretched and sits behind the neck, and a jacket hangs below the shoulders; with
  All-Stars Avatar's k, MaleBody, Head, a hairstyle and a jacket fit together front and side. Still inferred, not
  derived: **to confirm** against the console's own render (milestone 5).
- In the samples: tees have 2 meshes with weights and bone indices (skinned); hats and masks have 1 mesh with no
  weights or indices (rigid). Minimum y of the hats is 1.29, so units look like metres with the feet at 0.
- **Rigid meshes name their bone in the bases (2026-11-05).** A mesh with width 0 for weights and bone indices still
  has their u32 bases, and each vertex's value is the base plus nothing: every vertex the same. Over the pack's 334
  such meshes: 315 (the Head, all hair, hats, glasses, earrings) weight 0xFF on bone 0x13 = 19, Head; 14 rings on
  bone 46 (FingerRingRight); the 4 wedding bands on bone 39 (FingerRingLeft); "Slick Watch" weights 0x4CB3 on bones
  0x201F, 179 on 31 and 76 on 32, the left forearm bones by the wrist. So every mesh is skinned; the library's
  decoder gives these their bones like any other (research/: the bases are el[2] and el[4]).
- Header word 2 (= word 7) is the model's texture memory in bytes and word 6 its texture count (3.5). Between the
  header and the first mesh, from 0x51, are the material records (3.6).

### 3.4 Textures

Established 2026-11-04 on the console's pack (research/texture.py, tex_survey.py: all 187 texture blocks and all
1,696 textures in the 589 model blocks; decoded pictures checked by eye).

- A texture block (id 2) is compressed like a model block (3.2). Expanded, it is a 33-byte header and the pixels.
- **The header**, little-endian: byte 0 is the format as in a Xenos texture fetch constant: low 6 bits the data
  format (0x12 DXT1, 0x14 DXT4/5; no others occur), high 2 bits the endian mode (always 1, 8-in-16: swap the bytes of
  each 16-bit word before decoding). Byte 1 is 1 when the height is 128 or more, else 0 (inferred). Then u16 0x1A20
  (always), u32 width, u32 height, u32 allocated bytes (all frames), u32 allocated bytes per frame, u32 frame count,
  u32 a word whose bit 0 marks an empty slot (below) and whose upper bits are the width times the bytes per texel
  times 16 (inferred), u32 height again, and a zero byte.
- **The pixels are linear, not tiled**: decoding with the Xenos tiling formula garbles the 128x128 textures and
  addresses past the end of the 128x64 ones; decoding linearly gives clean pictures. Rows are the texture's own
  width; the allocated sizes are padded to a 128-texel pitch (a 32x32 DXT5 texture stores 0x400 bytes and is
  allocated 0x1000), so the GPU copy needs that pitch.
- Frames follow one another: eyebrows have 5, eyes and mouths 14 (expressions, presumably), others 1.
- The pack's texture assets are all DXT5, 128x64 (161) or 128x128 (26).

### 3.5 A model's textures

After the last mesh, with no padding, the model holds `word 6` entries of: u32 offset into the model's texture memory,
u32 allocated size, the 33-byte header of 3.4, then the pixels, unless bit 0 of the header word at 0x18 is set, in
which case the entry is an **empty slot** filled at run time. The offsets plus sizes end at word 2 in all 589 models.
Formats in models: DXT1 and DXT5, 32x32 to 256x256, mostly 32x32 (flat colours) and 128x128.

The Head is the one model with empty slots in all six entries: 128x128, 128x64, 64x64 x5 frames, 64x64 x14,
64x64 x14, 128x64. They match, in order, the face layers of All-Stars Avatar's `avatar_colours.py` (MIT): skin
features (component mask 0x20000, e.g. "Horizontal Stripe" 128x128), facial hair (0x10000), eyebrows (0x4000, 5
frames), eyes (0x2000, 14), mouth (0x8000, 14), eye shadow (0x40000). The pack's face textures fill them; they are
128 wide where the slots say 64, which doesn't matter for sampling. The other three face-feature masks are shapes,
not layers (3.7): nose 0x80000, chin 0x100000, ears 0x200000. Counted on the console's pack (2026-11-11), records
with exactly each mask: eyes 45, eyebrows 27, mouths 27, facial hair 17, skin features 26 (face paint, "Rosy
Cheeks", ...), eye shadow 45, noses 18 ("Defined Nose", ...), chins 9 ("Large Chin", ...), ears 9 ("Elven Ears",
...). `avatar.h` names all nine (`XSDK_AVATAR_SLOT_EYES` to `_EARS`).

**The face's frames** (2026-11-06; contact sheets of every frame, coloured as the Head shader colours them, of
Restful, Happy, Surprised and Wide Awake Eyes, Smirk, Gentle and Grin Mouth, Studious, Arched Eyebrows and Monobrow;
scratchpad renders, not kept):

- **The halves.** The Head's two meshes are its halves, mesh 0 at +x (the avatar's left) with material 0, mesh 1 at
  -x with material 1. Each material binds its own eyebrow and eye layer: parameter ids 7 and 9 on the left half, 8
  and 10 on the right; both bind the mouth with id 12 and the other layers with the same ids. In the eyebrow and eye
  layers' UV sets (2 and 3), u grows towards the ear on both halves (inside the layer, corr(|x|, u) +0.99 on each)
  and v downwards: **the texture's left edge is at the nose**, mirrored on the right half. The mouth's UV set (4)
  spans both halves, u 0.5 on the middle line.
- **The order is the same in every style.** XNA Game Studio 4.1's reference lists the same sets by name, without an
  order: AvatarEye (14), AvatarEyebrow (5) and AvatarMouth (14), and its AvatarExpression sets eyes and eyebrows per
  side. Matched to the pictures:
  - eyebrows: Neutral, Sad (the inner end raised), Angry (the inner end lowered), Confused (wavy), Raised;
  - eyes: Neutral, Sad, Angry, Confused (a heavy lid), Laughing (closed, arched up), Shocked (a small iris), Happy
    (a large iris), Yawning (narrowed), Blink (closed, arched down), looking up, looking down, looking towards the
    nose, looking towards the ear, Sleeping. Frames 8 and 13 are byte for byte the same in every eye style tried.
    XNA's LookLeft and LookRight can't be told apart from the pictures: looking to one side takes one eye looking in
    and the other out;
  - mouths: Neutral, Sad, Angry, Confused, Laughing, Shocked, Happy (frames 0 and 1 drawn in the style's own way,
    the rest alike in every style tried), then the seven lip-sync shapes, matched by their look (inferred): "o"
    (a round opening), "ai" (wide open), "l" (open, flat), "fv" (upper teeth on the lip), "w" (a small round
    opening), "dth" (the tongue between the teeth), "ee" (teeth together, wide).
- Neutral, Sad, Angry, Confused open all three, which supports the order. To check on the console: an avatar
  looking to one side looks that way with both eyes, rather than cross-eyed.

After the texture table comes another section, not decoded yet (u32 0x190 first in 295 of the models).

### 3.6 Materials

**A model is a series of parts** (found 2026-11-04 on sunglasses G1, whose lenses came out wrong (test run 3)): each
part is a 33-byte header (8 words and a byte), its material records, then its meshes. The first part's header is the
model bytes 0x30-0x50, after the model header's 12 words; a later part's header fills the gap before its first mesh
(until then the reader skipped those gaps and gave every mesh the first part's material). Records are 24 bytes,
little-endian: u32 type, u32 parameter id, 16 bytes of value, from the header's end to the part's first mesh. Checked on
all models in the pack except the two "Bald" ones, which have no mesh (research/material.py `parts`; the C reader
matches on all 589 and on G1 and C1): 543 have one part, 44 two (the Head's halves, suits, shades, earrings), 2 five
("Generic Avatar", C1).

**The part header's first word, a technique** (meanings inferred from where each occurs): 0 standard (486
one-part models: bodies, clothes), 1 hair worn under a hat ("High and Tight (Hat)"), 2 shiny (earrings, sunglasses
frames, the costume's five parts), 3 see-through (29 of 31 two-part sunglasses' lenses, and "Surfer Shades"), 4 the
Head (both halves). G1: the frame part binds texture 0 (a "CREATURE" print, the temple arms on the
dashboard), the lens part binds texture 3 as base (green, alpha about 0.67), texture 4 (yellow stripes) as reflection
with float4 0x19 x 0.4, and float4 0x1A 0.083 grey. The viewer draws technique 3 last, blended by the base texture's
alpha (inferred; on the console to compare).

- **Type 3**, a float4: ids 0x16, 0x17, 0x18, 0x1B in every item (all (0, 0, 0, 1)), 0x19 in 82 (x mostly 0.5:
  earrings, glasses; shine, inferred), 0x1A in 5. The Head has 0x0D-0x15 and 0x1B instead.
- **Type 1**, a texture binding: u16 texture index, u16 UV set, u32 address mode (3 wrap, 0 clamp; 0 in 26 records, the
  Head's eyebrow, eye and mouth slots among them), 8 zero bytes. Checked by drawing (2026-11-04): wrapping the Head's
  mode-0 slots leaves copies of the eyebrows and mouth at the temples and cheeks (seen on the console too); clamping
  them removes those and changes nothing else. Items bind ids 1, 2 and 3 (or 4 with float4 0x19). The Head binds slot n
  to UV set n as ids 5, 6, 7, 9, 0xC, 0xB.
- Roles, from the pictures: **id 1 the base colour** (denim, a flat skin tone on the body), **id 2 the tint mask**
  (solid red on the body, red with fabric shading on a tee, transparent on jeans), **id 3 a decal** with alpha (a
  tee's logo, transparent elsewhere). **Id 4** binds UV set 0xFFFF, none, in all 82 pack models that have it and in
  the item samples (two masks, a tee); on the earrings its texture is black with grey vertical stripes: a reflection
  map looked up from the normal, its strength float4 0x19 x (inferred; on the console to compare).
- The C reader (`xsdk_avatar_model_decode`, `xsdk_avatar_texture_decode`) matches the prototypes on the whole pack
  (589 models, their 1,619 textures and materials, and 187 texture blocks; tests/avatar_dump with research/check_c.py,
  pixels compared by FNV-1a) and decodes all 10 item samples.

**The colour rule**, from All-Stars Avatar's `avatar_colours.py`, checked by drawing (research/texrender.py,
2026-11-04: description P1 with MaleBody, Head, Raised Front, Jeans and the Male Power Tee, front and
side; all parts come out in their colours and the face features sit in place):

- Items: `lerp(base, mask.r * C1 + mask.g * C2 + mask.b * C3, mask.a)`, then the decal by its alpha (ours; All-Stars
  Avatar doesn't cover it). C1-C3 are the item's custom colours from the description, or, where it gives none (alpha 0),
  the first entry of the item's colour table (3.1; untinted if it has none), the body takes the skin colour for all
  three and hair the hair colour (both inferred by All-Stars Avatar).
- The Head: start from the skin colour; per slot in order, `lerp(colour, r * A + g * B + b * C, a)` with (A, B, C)
  = (feature 1, feature 2, skin), (facial hair, white, skin), (eyebrows, white, skin), (eyes, white, skin),
  (mouth, white, skin), (eye shadow, black, black). Frame 0 of the eye and mouth textures gives a neutral face.
- Byte to float as byte / 255 (sRGB or not is unknown). Lighting is still flat; the console's look is 5.3.

### 3.7 Shape overrides (block 4) and hiding

Established 2026-11-04 on the console's pack (research/shape.py: all 267 shape blocks; the C reader,
`xsdk_avatar_shape_decode` and `xsdk_avatar_pack_link`, gives the same on all of them and on every record's link),
prompted by the viewer's first textured run: skin showed through skinny jeans and a skirt.

- Block 4 is not compressed. Little-endian: u32 n, u32 the target model's index bytes (its header word 4: 0x3510
  MaleBody, 0x3984 FemaleBody, 0x13D4 Head), the target's stock id (u32 mask, u16 record, u16 gender, the stock
  tail), u32 0, u32 w, then **n triangle numbers of w bits each, LSB first: the target's triangles to hide**,
  ascending, numbered across its meshes (w is 12 for the bodies).
- **Then the vertex overrides**: u32 n, u32 the target's vertex bytes (header word 3), the target's id again, u32 0,
  u32 w (16), then a mesh's quantisation header (f32 step, f32 min x, y, z) and bit stream header (3 x 6-bit position
  widths, the four elements' 32-bit base and width), then n entries: **w bits of byte offset into the target's
  unpacked vertex buffer, then a whole vertex as a mesh stores it** (position, normal, weights, bone indices,
  colour; no texture coordinates). The unpacked vertex is 28 bytes plus 2 per texture-coordinate half (MaleBody and
  FemaleBody 40, the Head 52), so offset / stride is the target vertex, numbered across its meshes. Found by the bit
  count (274 + n x (w + the vertex's bits) ends every block exactly) and by matching: the jeans template's first
  offsets 5240, 5400, ... over 40 are MaleBody vertices 131, 135, ... at distance 0; the chin's, over 52, Head
  vertices 341, 342, ... moved 6 mm on average. Checked on all 267 blocks: every offset is a whole vertex of the
  target, every block ends with its last entry, the C reader (`xsdk_avatar_shape_decode`, `_apply`) matches research
  on all of them and applies each to its target. The 36 face shapes (chins, noses, ears, ...) are only this part and
  move Head vertices up to 4.6 cm; 221 hiding templates also move body vertices (up to 5.5 cm, pulling the body in
  under the garment, presumably), 10 body templates only move them (high heels). Checked by drawing: P1's
  "Rounded Chin", "Sharp Nose" and "Round Ears" visibly reshape the head (research, 2026-11-04).
- 221 blocks hide body triangles (113 MaleBody, 108 FemaleBody), 10 target a body and hide none, 36 the Head.
- **The link**: a stock pack record's head holds a related asset's stock id at bytes 0x9C-0xAB (big-endian, as in
  descriptions). For 266 clothes it names a body template (mask 0x01000000): "... Hiding Template" (240) or another
  "... Template" ("Male Jeans Template", "High Heel Body Modification Template"); several clothes share one. Hair
  links to its variant under a hat (mask 0x4; 88 hairs, e.g. "Raised Front" -> "Quiff (Hat)", whose part
  technique is 1), eye shadows to a partner (0x40000). **Used since 2026-11-11**: with an item filling the hat
  slot, each stock hair is swapped for its variant (avatar_outfit.c `hair_under_hat`); before, the full hair poked
  through hats (test run 9: "Raised Front" through a Santa hat). Checked on the PC: `outfit_check <pack> hat`, and
  a render of the rig export, "Raised Front" showing through a Male Baseball Cap and "Quiff (Hat)" staying under
  it; on the console the same day under P3's Santa hat (console run 2026-11-11b). Hair from an item package has
  no pack record, so no link: it is left as it is (not seen yet). Marketplace items carry their own block 4 (the
  tee samples).
- Checked by drawing (2026-11-04): stock "Punk Jacket and Tee" with "Black Skinny Fit Jeans" on MaleBody shows skin
  through the jeans; with the two templates' 666 triangles left out of the body it is clean.

## 4. Where an avatar's parts come from

| Part | Where | Status |
|---|---|---|
| Marketplace and award items | STFS packages, `Content\<xuid or 0>\<title id>\00009000\` | known; Avatar Thrift Store reads them |
| The description (1000 bytes) | the profile: a dashboard GPD setting | the size is known (360sona, PR #3 of Avatar Thrift Store); the setting id and layout are to find (5.1) |
| Stock body, heads, hair, eyes, default clothes | system content, likely an "avatar asset pack" | **unknown** (5.1) |
| Skeleton | probably the stock pack (block 5) | **unknown** |
| Animations | probably the stock pack (block 1) | **unknown**, later milestone |

All-Stars Avatar decodes the same model format from an `AvatarAssetPack` with an `AvatarAssetPack.toc` that ships
inside a game, which suggests the system keeps its stock assets in the same kind of pack.

**Where item packages can be** (2026-11-12): any storage device the console has, each with a `Content` folder at
its root. The console's `\Device` object listing (probe_pack.log, EVIDENCE.md "Device listing") has the hard drive
(`Harddisk0`, a directory of partitions; content on `Partition1`), the USB ports `Mass0`, `Mass1` and `Mass2`, the
memory unit slots `Mu0` and `Mu1` (with `Mu0System`, `Mu1System`), the internal NAND memory unit of 256 and 512 MB
Arcade consoles `BuiltInMuSfc` (with `BuiltInMuSfcSystem`), and `BuiltInMuUsb`, a directory like `Harddisk0`: the
4 GB slims' internal flash, its content partition taken to be `Storage` (inferred, from an outside bug report; not
yet seen on a console). Content at the hard drive's and USB devices' roots is established (Avatar Thrift Store
installs items there); at the memory units' roots it is inferred. The library searches the shared folder
(`0000000000000000`) of each, in the order of `item_folders` in avatar_content.c; items kept under a profile's
own folder (`Content\<xuid>\`) are not searched yet.

### 4.1 The stock asset pack (`AvatarAssetPack.toc`)

Source: All-Stars Avatar, `src/avatar/tools/nexia_pack_adapt.py` (MIT), for version 1 on that game's disc; version 2
checked on the console's own pack (4.3) with research/toc.py.

- Big-endian. The header: 16 bytes (an id?), 8 bytes `01 C8 F1 09 A1 9C B2 E0` (the same 8 bytes end the stock
  assets' ids in All-Stars Avatar), u32 version at 0x18, u32 record count at 0x1C (1282 in both versions).
  - Version 1: records from 0x20, 13 locales.
  - Version 2 (the console's): u32 at 0x20 (2), u32 locale count at 0x28 (18), records from 0x30.
- Records of `8 + 0xB4 + 4 * locales + 8` bytes (0xF8 in version 1, 0x10C in version 2): a u32 component mask, then
  gender (1 male, 2 female, 3 both), two flag bytes; 0xB4 bytes mostly zero (colour defaults for sports tops); one
  u32 offset per locale to a UTF-16BE name; the u32 offset of the asset's STRB blob and its u32 size.
- The console's pack: 1282 records, 1103 with a blob, 179 empty (mask 0, flags 0x200, no name, no blob). Blobs by
  block set: 547 model, 267 shape override (the "... Hiding Template" records and face shapes like "Square Chin"),
  187 texture (eyes, eyebrows, mouths), 60 animation, 39 model with colour table (sports tops), 3 model with
  skeleton (the sheep and teddy-bear sleep props).
- Records 0-2 are **MaleBody**, **FemaleBody** and **Head**; 3-43 the stock animations ("Animation Generic Stand 0"
  to 7, Clap, Wave, Celebration, the male and female emotions and idles, the avatar creator's look directions).
- **No skeleton for the avatar itself**: only the three props carry a skeleton block. The avatar's skeleton is in
  the body model's data or in XAM's code; to find (5.2).
- The blobs are the STRB files of section 3. **All 589 model blocks in the pack decode** with the layout of 3.3
  (research/sweep.py): every mesh's index header validates at exactly one texture-coordinate count, and the meshes'
  number and index bytes equal header words 5 and 4. "Bald" and "Bald (Hat)" have zero meshes. 300 meshes are
  skinned; texture coordinates are 4 halves (115 meshes), 6 (517) or 12 (the head's two halves).
  MaleBody: 1137 vertices, 2264 triangles; FemaleBody: 7362 indices; Head: two mirrored halves of 324 vertices.

### 4.2 Not in flash

The console's flash system partition (System, 2026-11-03: xam.xex, dash.xex and 34 other files) holds no
asset pack. Its only avatar trace is `XAVATAR` among the static libraries xam.xex was linked with (the XEX's library
versions header, file offset 0xD3C), beside `D3D9XA`, `XUIRNDR` and `XGRAPHC`: xam carries the avatar code itself.
None of the resources xam.xex embeds (fusion, controlp, gamercrd, shrdres, ...) is avatar data. The XEX bodies are
encrypted, and decrypting them needs Microsoft key material, which the rules exclude (section 2), so their strings
weren't searched.

### 4.3 The system extended partition (SysExt)

From a copy of the console's system extended partition (SysExt, 2026-11-03), read with
Avatar Thrift Store's `avtool`:

- `SysExt:\system.manifest` (magic `XMNP`) lists the system packages and the files in each. One package,
  **`FFFE07DF00000002`**, holds the stock asset pack: `\SEP\Content\0000000000000000\FFFE07DF\00008000\
  FFFE07DF00000002` (`\SEP` is a symbolic link to `\Device\Harddisk0\SystemExtPartition\`). FTP couldn't copy it;
  tools/probe_pack opened it with all sharing allowed and copied it (console run 2026-11-03, the Pack in
  EVIDENCE.md), so a title can read it directly while the system runs.
- The package (PIRS, display name "AvatarAsset", 16,064,512 bytes) holds `AvatarAssetPack.toc` (14,662,168 bytes)
  and `AvatarAssetPackLegacyV1.toc` (1,252,000 bytes, not looked at yet).
- Its neighbours in that folder, all PIRS packages of title `FFFE07DF`, content type 0x8000: `...0001` "ParityPack"
  (achievement crowns, NUI camera firmware), `...0006` "DashPack" (sign-in sleep animations, themes, wallpapers),
  `...0008` "NuiPack" (Kinect identity and speech data).
- DashPack's `Sleep_*.bin` and `SleepSelect_*.bin` (13 files) are STRB assets whose metadata says animation (type 4)
  for both genders: real avatar animations to work out the animation block (id 1) on, for milestone 7.
- The kernel's object directories, listed by the probe: `\` holds `sep` -> `\Device\Harddisk0\SystemExtPartition\`,
  `sap` -> `\Device\Harddisk0\SystemAuxPartition\`, `SystemRoot` -> `\Device\Flash`, and the directories `Device`,
  `??`, `TitleNamedObjects`, `SystemNamedObjects`. `NtOpenDirectoryObject`, `NtQueryDirectoryObject`,
  `NtOpenSymbolicLinkObject` and `NtQuerySymbolicLinkObject` take the original Xbox kernel's arguments (nxdk); the
  entry type is a tag (`Dire`, `Symb`, `Devi` as big-endian ASCII, logged as 65726944, 626D7953, 69766544).
- `20449700\` holds the avatar editor (`AvatarEditor.xex`, 18 MB, encrypted) and `Guide.AvatarMiniCreator.xex`. They
  aren't needed for the format, and disassembling them is not part of this project's evidence.

### 4.4 The skeleton

Not stored as data anywhere we can read (2026-11-03):

- Neither pack (`AvatarAssetPack.toc`, version 2, 1282 records; `AvatarAssetPackLegacyV1.toc`, version 1, records
  from 0x28, 44 records: older revisions of records 0-43, metadata version 1) has a skeleton block for the avatar.
  Only three props (sheep, teddy bear) carry one (block 5: bit-packed, the same quantisation step as meshes).
- XAM exports about 30 avatar functions (xenia:src/xenia/kernel/xam/xam_table.inc, ordinals 0x5DC-0x5F8:
  `XamAvatarGetManifestLocalUser`, `XamAvatarGetAssets`, `XamAvatarGetAssetBinary`, `XamAvatarLoadAnimation`, ...),
  none for a skeleton. The skeleton is a constant in Microsoft's avatar library (`XAVATAR`, linked into xam.xex and
  into every title that renders avatars), out of bounds for this project (section 2).

**The skeleton block (id 5)**, decoded 2026-11-04 on the three that exist (the "Generic"/"Snore Sheep" (18 bones),
"TeddyBear Sleep" (8) and Prop1 (12); research/skeleton.py). Little-endian, not compressed:

- 8 u32 header words: block length - 12, 0, block length - 0x1C, a word ending in 0x3000, three words not
  understood (29 14 1, 1 26 1, 37 27 9), then **the bone count**.
- From 0x20 a bit stream (LSB first): a **position group** (f32 step = the mesh step, f32 min x, y, z, three 6-bit
  widths) and a **rotation group** in the same form (minimums like -pi/4, -1.44: angles in radians), then **per bone:
  u8 parent (255 for the root), the quantised position, the quantised rotation**. Checked: every block ends at its
  last byte exactly; only "parent leads each record" gives a valid tree in all three (the sheep: root, body, six
  children, two-segment legs; the launcher: root, seven children, one with four).
- **Positions are rest-pose world positions on the mesh scale**: `min + q x step x k` with the meshes' k (3.3).
  Checked on the launcher, whose rigid hexagons hang one on a bone each: vertex centre over bone position is exactly
  (2.00, 1.63, 1.73) on every one of the six.
- **Rotations (settled 2026-11-05, research/skeleton.py `rest`)**: dequantised like positions, `min + q x step x k`
  with the same per-axis k, they are each bone's **rest orientation in the mesh's space** (not relative to the
  parent) as a **quaternion logarithm**: the rotation axis times **half** the angle. The rig is a clean one: the
  sheep's root is exactly the identity; every bone's local **+x points at its child** (cosine 1.0000 on all eight
  leg bones of the sheep); local **z is the hinge axis** (the sheep's legs, body, head and tail: z along the
  sheep's sideways axis); left and right legs and ears are exact mirror images, and the teddy bear's arms and legs
  likewise. How it was found: of 6 Euler orders, rotation vectors at 0.5, 1 and 2 times the value and quaternion
  parts, each with and without k, as world or parent-relative, transposed or not, only this one puts one local axis
  along every leg bone (score 1.000; the next 0.919).
- Posing a prop with this skeleton and its animation (4.5) through research/pose.py keeps every part attached; the
  sheep jumps, the teddy bear's limbs swing. Careful: k multiplies `q x step` only, not the minimum; scaling the
  whole value puts each part a few centimetres off its bone.

What the data does give:

- **71 bones.** The body meshes use bone indices 0-70; clothes use subsets (a tee 0-30). Each vertex's four weights
  sum to 255. Bone names: the public XNA Game Studio 4.1 reference, "AvatarBone Enumeration"
  (learn.microsoft.com, dd940212), names 55 of them, e.g. Root 0, BackLower 1,
  HipLeft 2, HipRight 3, BackUpper 5, KneeLeft 6, KneeRight 8, AnkleLeft 11, CollarLeft 12, Neck 14, Head 19,
  ShoulderLeft 20, ElbowLeft 25, WristLeft 33, the fingers 37-70, PropLeft 41, SpecialLeft 42. It says "bone indices
  are sorted by depth value", so a parent's index is below its children's, and that the unnamed indices are
  special bones. The body skins none of the named pivots 6, 8, 14, 20, 22, 25, 28: its skin sits on unnamed helper
  bones beside them (7/9 thighs, 13/17 shins, 27/30 upper arms, ...).

**The avatar's skeleton, derived (2026-11-05; research/avatar_skeleton.py).** Built from the data and checked by
posing MaleBody and FemaleBody through stock animations; the library will ship it as a table of our own:

- **Parents**: the named bones by anatomy, the unnamed ones by where their skin sits, and the whole table satisfies
  "sorted by depth" exactly (depths by index: 0; 1-4: 1; 5-10: 2; 11-18: 3; 19-24: 4; 25-30: 5; 31-36: 6;
  37-50: 7; 51-60: 8; 61-70: 9). Root 0 -> BackLower 1, HipLeft 2, HipRight 3, pelvis skin 4. BackLower 1 ->
  BackUpper 5, lower spine skin 10. Hips 2/3 -> knees 6/8 and thigh skin 7/9; knees -> ankles 11/15 and shin skin
  13/17; ankles -> toes 21/23. BackUpper 5 -> collars 12/16, Neck 14, chest skin 18; Neck 14 -> Head 19, neck skin
  24. Collars -> shoulders 20/22; shoulders -> elbows 25/28, upper-arm skin 26/29, upper-arm twist 27/30. Elbows ->
  forearm twist 31/34, forearm skin 32/35, wrists 33/36. Each wrist -> index, middle, ring, small, prop, special,
  thumb (37-43 left, 44-50 right); each finger and thumb two more joints (51-60, 61-70).
- **Rest orientation: the identity for every bone.** The body is in a T-pose (arms along x, y up, the avatar facing
  +z, its left at +x, the shoulders at y 1.05) and the bones keep the world axes, unlike the props' rigs. The animations
  show it: knees and toes turn about x only, elbows about y (98%), fingers about z (93-100%), the twist bones
  27, 30, 31, 34 about x only; with the arms along x those are exactly the anatomical hinges.
- **Rest positions**: each bone's pivot is the weighted centre of the vertices its subtree shares with the rest of
  the body (by the four skin weights); the root is midway between the hip joints. Per body: MaleBody and FemaleBody
  differ (hips 0.069 vs 0.080 from the middle, shoulders 0.122 vs 0.109). The helper bones that no animation moves
  (4, 7, 9, 10, 13, 17, 18, 24, 26, 29, 32, 35) only follow their parents, so their positions don't matter; the
  prop and special bones (41, 42, 48, 49) have no skin and no position yet (props, GOALS.md 4.3).
- **Checks, both bodies** (with 4.5's values): "Generic Clap": the wrists come to 0.066 (male) and 0.043 (female)
  apart, from 1.02 in the T-pose; "Generic Wave": the right forearm rises with the hand open by the head;
  "Generic Stand 0": arms hang at the sides and the lowest foot vertex stays within 0.0001 over the whole
  animation; "Animation Female Angry": hands on the hips, through the frames where the elbow's value changes sign.
  The root's height is the least certain value: foot sliding over 34 standing animations is about equal for any
  height from 0.66 to 0.8 (and 35% worse at the floor); to check against the console.

### 4.5 Animations (block id 1)

Established on all 60 stock animations (research/anim.py, anim_survey.py):

- Header, little-endian: u32 frame count, f32 frames per second (about 20; 14 for the prop animations), u32 bone
  count (71), u32 extra bones (0; 18 and 8 for the sheep and teddy-bear animations: the props' own), u32 3, u32 5,
  four u32 offsets to sections after the frame data, then u32 frame count and u32 bone count again.
- From 0x30, a bitstream (LSB first) of **213 channel headers = 71 bones x 3**, 146 bits each: f32 step
  (7.071068e-6, as meshes), f32 x, y, z, three 6-bit widths. Width 0 means the axis is constant at that value.
- Then the frame data: per frame, per channel, per axis, `width` bits. Checked: the frame data ends 14 bytes before
  the first section offset in every animation tried.
- Per bone the three channels are, it appears, a translation (near zero, never animated), a rotation (animated;
  values like (-0.18, 0.13, 0.07)) and a scale (always (1, 1, 1)).
- **Not yet right:** decoding the frames as `value + q * step * 2` gives rotations above 1 and motion spread over the
  whole body in "Wave", so the scale of the stored values, or which channel is which, is still wrong. Of three
  orders tried for the frame data (frame, channel, axis outermost), frame-major gives the smoothest curves (median
  step 0.125 of each field's range, against about 0.3 for the others and for noise), but still far too jumpy for
  motion: the stored values are probably not plain samples (deltas, keyframes or another coding). The sections the
  four offsets point to (holding a frame count and the 3 and 5) are not decoded either; they may say how.
- **What a channel holds** (2026-11-04): **changes from the rest pose, in each bone's own frame**, not the pose
  itself. In "Generic Stand 0" every translation is 0 except bone 70's few millimetres, every scale 1, and most
  rotations move about a single axis (z) like hinges, mirrored between left and right bones (+0.233 / -0.233). The
  sheep's own channels in "Generic Sheep Interact" are the same: translations about 0, rotations about one axis,
  none equal to the sheep skeleton's rest rotations. So posing needs each bone's rest position **and orientation**.
- **The frame data, 2026-11-04, still open.** The avatar part fits a plain layout (frames x the animated fields'
  widths) to within 118 bits in "Generic Sheep Interact", as in all stock animations; the prop part (section A) does
  not: 64 x 760 bits ends near byte 20,648, the section near 23,564. Read plainly in frame order, the avatar fields of
  "Generic Stand 0" are of three kinds: smooth (bones 9, 14, 15); **smooth but wrapping at 2^w** (bone 2's z: ...
  7994, 7995, then 315 = 8507 - 8192; bones 15 and 23 alike), which suggests an angle of `min + 2 pi q / 2^w`, a full
  turn over the field's range, rather than `min + q x step`; and jumping by exact quarters of the range (bone 10's y
  near 848 + 2048 k). Frame 0 is 0 in every field. The sheep's own fields are 0 for frames 0-30 (the sheep waits),
  then partly smooth; the launcher's hold one value for 13 frames and jump all together, some "scale" fields with
  values unlike scales. So the field order or meaning within a frame is not settled.
- **The frame layout, solved (2026-11-04; research/anim.py `frames`)**: **the channel table lists the bones
  backwards** (table entry t is bone 70 - t); three empty channel headers (438 zero bits) follow the 213; then the
  frames, P bits each, **bones 0 to 70 in order**, per bone translation, rotation, scale and per channel x, y, z,
  each animated axis `width` bits, LSB first. The frames can run past the header's section A offset (by 40 bytes in
  "Generic Stand 0" and "Generic Wave"). Checked on all 60 stock animations: 5,732 of 5,797 animated fields (98.9%)
  are continuous curves (95% of steps under a quarter of the field's range), against about 10% in the header's
  order; the lowest, "Generic Clap" (84%), is fast hand motion. The bone numbering agrees with the XNA names
  independently: the widest rotation fields (the most motion) in "Generic Wave" are WristRight 36, ShoulderRight 22,
  WristLeft 33, ElbowRight 28, ElbowLeft 25; in "Generic Clap" both wrists, shoulders and elbows; in "Generic Stand
  0" both elbows.
- **Every channel table has 216 slots** (72 bones x 3; 2026-11-05): the used headers, then zero bits for the unused
  ones (3 in the avatar part, 162 in an 18-bone prop's), then the frames. **The prop part** follows the avatar
  part's last frame at the next byte: u32 frame count, u32 bone count, a table and frames in the same form (bones
  listed backwards, frames bones 0 to n-1). In "Generic Sheep Interact" it ends exactly where section B's u32 64,
  u32 3 begin (byte 23,604), and all 82 animated sheep fields are smooth loops (forward bone order: 18 noisy). The
  header's section A offset points 40 bytes before the prop part (it ends inside the avatar frames); what it marks
  is not known.
- **The values (settled 2026-11-05, research/anim.py `values`, research/pose.py)**: each axis is
  `min + q x step x k` with the meshes' per-axis k (the sheep's scale fields give 1.2000 on all three axes only
  with k). Per bone and frame: a **translation** and a **rotation as a quaternion logarithm (axis times half the
  angle, as in the skeleton block)**, both in the bone's **own rest frame**, and a **scale**; the bone's local
  transform is its rest local transform (parent's rest frame^-1 x own) followed by translate(T), rotate(R),
  scale(S). Checked: the sheep stays whole and leaps along its own heading, legs stretched fore and aft, at 1.5-1.9
  above the ground (a dream above the sleeping avatar's head), popping in and out with scale 0; translations in the
  parent's frame would put it 1.4 below the ground.
- **Half the angle, and either sign of the quaternion.** The values sometimes jump by exactly pi along their axis
  (an elbow in "Animation Female Angry": -0.99, -1.04, then 2.08, 2.09, ..., back to -1.05): v and v + pi x axis
  are q and -q, the same rotation, and the encoder switches between them. This is also why no length passes
  2 pi / 3 (a first reading, 2026-11-05, took it for a cap on whole angles and was wrong). Decoded as whole angles
  the avatar's arms stay half raised in "Generic Stand", the hands never meet in "Generic Clap" (0.68 apart at the
  closest) and the waving forearm swings forward instead of up; as half angles all three come out right (4.4).
- How the layout was found (the earlier notes below record the way there): bits exactly P apart agree far more
  often than any other distance, so frames are P bits; per bit position, how often a bit keeps its value into the
  next frame shows field boundaries (low bits flip, top bits hold), and those were the header's widths with bones
  in another order; every animation starts with the same 438 zero bits after the table.
- **Frames, earlier notes (2026-11-04, research on "Generic Stand 0")**: the data is stored **frame by frame, P bits a
  frame**, P the sum of the animated fields' widths (800 here): bits P apart agree 64.5% of the time, bits at any
  other distance 50-52%, with a second peak at 2P; any extra bit per frame destroys what smoothness there is. Within
  a frame, a bit's stability from frame to frame (low bits of a field flip, top bits hold) shows the field
  boundaries: they are **the header's widths in another order**. In "Generic Stand 0" the frame begins with bone
  36's rotation (9 bits), bone 42's (5, 14, 2), bone 40's (8), bones 2, 14, 19, 23 (13 each), bone 55's (9, 8, 11),
  then 9, 24, 32, 11, ...; decoded in that order 42 of 81 fields are smooth curves against 7 in header order. A
  search over orders stops there, and other animations give other orders: whether the order is per animation (and
  stored somewhere: sections B and C, or the 14 bytes after the frames?) or follows a rule (the skeleton's order?)
  is the open question.
- **The sections** the four header offsets point to: A, near the prop part (above: a second channel table and frame
  data for the extra bones, u32 frames, u32 bones = 18 for the sheep, then its table); B and C, tables of
  5 and 3 channels (header words 4 and 5; presumably face tracks, to settle); D, 40 bytes: 0, then a 3x3 identity
  matrix as big-endian floats (a root transform, presumably). None holds a skeleton for the avatar.
- **The hierarchy and joint positions, inferred** from MaleBody's skin weights (research/infer_skel.py): bones that
  share vertices are joined, a maximum spanning tree over the shared weight from bone 0 gives the tree, and the
  weighted centre of the shared vertices gives each joint. 60 of the 71 bones are reached, in a coherent,
  left/right-symmetric body: root 0 -> pelvis 4 -> hips 7/9 -> knees 13/17 -> ankles 11/15 -> toes 21/23; spine 10 ->
  chest 18 -> neck 5 and head 19/24; collarbones 12/16 -> upper arms 27/30 -> elbows 26/29 -> forearm twists 32/35 ->
  wrists 31/34 -> hands 33/36 -> five fingers of three joints (37-70). Bone indices are not ordered parent first.
- Superseded (2026-11-05) by the derived avatar skeleton in 4.4: the tree above joins skin regions, not bones; the
  unskinned bones (6, 8, 14, 20, 22, 25, 28) turned out to be the joints the animations drive, and 41, 42, 48, 49
  the prop and special bones.

### 4.6 The avatar description

Read on the console (tools/probe_manifest, 2026-11-03: P1):

- `XamAvatarGetManifestLocalUser(user index, 1000-byte buffer, NULL)` (xam 0x5DE) returns 0 and fills the buffer
  synchronously, without `XamAvatarInitialize`.
- `XamUserReadProfileSettings(title, user, 0, NULL, 1, {0x63E80044}, &size, buffer, NULL)` (xam 0x219) returns the
  same 1000 bytes at buffer offset 0x30, after a header (count 1, a pointer to the record) and the setting record
  (id 0x63E80044, type 6 binary, size 0x3E8, data pointer), with title 0 and with FFFE07D1 alike.
- The layout All-Stars Avatar's `avatar_desc.py` (MIT) gives fits it exactly (research/manifest.py): two floats at
  4 and 8 (0 and 0 here; height and weight, inferred), 15 face-feature ids at 0x0C, 9 ARGB colours at 0xFC (skin,
  hair, mouth, eyes, eyebrows, eye shadow, facial hair, two feature colours), 15 item entries at 0x120 and 4 at
  0x300 (16-byte id, u16 slot mask, u16 0, 3 ARGB custom colours), 16 bytes at 0x380, zeros after.
- **Item ids.** A stock item's id is u32 component mask, u16 record index in `AvatarAssetPack.toc`, u16 gender,
  then `C1 C8 F1 09 A1 9C B2 E0`. Every stock id in P1 names a record whose name and mask fit
  (MaleBody, Head, "Raised Front", "Rounded Chin", "Smirk Mouth", "Jeans", "Male Power Tee", ...). Any other id is a
  marketplace or award item's asset id, ending in its title id; its package is named after the id in 32 upper-case
  hex digits (Avatar Thrift Store's README), e.g. `Content\0000000000000000\AAAAAAAA\00009000\<id>`.
- The second list (0x300) held the outfit under C1 (shoes, "Jeans", "Male Power Tee" with a custom
  colour, the hair): presumably what the costume replaces. Inferred.

### 4.7 360sona's rigged export (milestone 10)

Checked 2026-11-06 on R1, a 360sona export of P1's avatar (glTF 2.0, generator "360sona"; not in the repository:
it holds the console's meshes and textures):

- **The skeleton is ours, bone for bone.** One skin of 71 joints in exactly the library's order (4.4), with the same
  parents. Named bones are humanoid names (Hips 0, Spine 1, LeftUpperLeg 2, Chest 5, LeftLowerLeg 6, LeftFoot 11,
  LeftShoulder 12, Neck 14, Head 19, LeftUpperArm 20, LeftToes 21, LeftLowerArm 25, LeftHand 33, the fingers 37-70
  with PropLeft 41 and SpecialLeft 42); the helper bones are `shape_NN`, NN our index.
- **The same space.** The body mesh has MaleBody's bounds exactly (x +-0.711, y 0.005 to 1.167, z -0.103 to 0.149):
  metres, y up, facing +z, with the per-axis scale of 3.3. Seven meshes (body, head, hair, shoes, trousers, hat,
  shirt), each skinned.
- **The same rest orientations.** The inverse bind matrices give a T-pose with every bone on the world axes (0.0
  degrees off, scale 1), as ours (4.4). The rest pivots differ: hip joints at x +-0.090 (ours +-0.069), the root
  (Hips) at y 0.777 (ours 0.646, midway between the hip joints), up to 12 cm on some bones. The nodes' own transforms
  are a pose, not the rest pose, scaled 1.092 (the helper bones 1.35-1.56): it seems the description's body shape
  (4.6), height as the joints' scale.
- **Its animations are ours.** The file carries one, "Hooray!": 57 rotation channels, translations on Hips and the
  prop bones, 68 keys 0.0502 s apart (19.90 a second), linear. Its rotations equal the library's decoding of
  "Animation Generic Celebration" (4.5) to 4 decimals on every bone compared at 0.5 s and 1.5 s: each joint's local
  rotation is the change from its rest orientation, exactly our `XsdkAvatarBonePose.rotation`. That also confirms our
  decoder from an independent source. The root's translation is absolute and scaled: with its rest pivot p (theirs)
  and ours q, its rotation R and scale s, theirs = s (R (p - q) + q + t), t ours (matches to a few millimetres).

So a 360sona export is a rig to animate in Blender (GOALS.md 4.5): animations made on it convert by copying
rotations, and re-basing the root's translation by the formula above. Our own rig exporter (4.9) is the other source.
Its animation must not be shipped: it is a stock animation (GOALS.md principle 5).

### 4.8 The library's own animation file (milestone 10)

Our own format, for animations made in a 3D program (GOALS.md 4.5), written by `tools/anim_convert.py` (Python,
standard library only) and read by `xsdk_avatar_animation_load`. Little-endian:

| Offset | Size | What |
|---|---|---|
| 0 | 4 | "XAAN" |
| 4 | u32 | version, 1 |
| 8 | u32 | bones, 71 |
| 12 | u32 | frames (the last repeats the first in a loop, as in the stock animations) |
| 16 | f32 | frames a second |
| 20 | f32 x 3 | the rig's root pivot at rest, in the meshes' space |
| 32 | per frame: f32 x 3, then per bone f32 x 4 | the root's move from that pivot; each bone's rotation (x, y, z, w) |

A frame is 1,148 bytes: about 23 KB a second at 20 frames a second. Rotations are the library's pose rotations
(4.5): the change from the rest pose in the bone's own rest frame. The converter makes them from the glTF's local
rotations L and the bind orientations B of the inverse bind matrices, B_parent L B^-1 (L itself for a 360sona rig,
4.7), matches joints to bones by order when the skin's tree is ours (else by name), resamples at the keys' own rate
when they are even (else 30 a second), divides out the skeleton's scale, and leaves out other translations and all
scales. The loader keeps each frame's rotations on the side of the one before.

The root's pivot: given the skeleton of the avatar that plays it, the loader moves the root's motion from the rig's
pivot p onto the skeleton's q, t + (R - 1)(q - p), so that the body turns about the point it turned about in the 3D
program; given NULL it takes the move as it is. Checked 2026-11-06 (`tests/anim_check.c`): 360sona's "Hooray!"
converted (68 frames at 19.901 a second, joints matched by order, pivot (0, 0.777, -0.009)) and loaded plays as the
stock "Animation Generic Celebration" decoded from the pack: every bone's rotation within 0.08 degrees at 200 times,
the root's move identical (0.0000 m) when loaded without moving it (360sona put the stock moves on its rig
unchanged, as if its pivot were ours; moved onto the male skeleton's pivot it would differ by up to 5 cm). So for
360sona's own exports of stock animations, NULL; for animations made in Blender on its rig, the skeleton.

Through Blender, checked 2026-11-06: R1 imported into Blender 5.1.1 (glTF importer, defaults) and exported
again (selection, actions, sampled every frame at the scene's 24 a second) keeps its bind matrices axis-aligned, but
reorders the skin's joints, so the converter matches them by name. Converted, the "Hooray!" re-export equals the
original's conversion within 0.03 degrees on every bone at the export's own frames, the pivot and the root's move
within 8 mm. This found a fault in the converter's name table (the right hand's middle and end finger joints were
numbered as the left's, two too high), fixed the same day.

### 4.9 Our own rig (milestone 10)

The library writes a rig to animate on the console, from its own files, so the workflow of GOALS.md 4.5 needs no
website and nothing copied off the console first: `xsdk_avatar_export_profile`, `_description` and `_stock`
(src/avatar_export.c, beside the three loads) give a glTF binary; `samples/rig_export` writes `rig_male.glb`,
`rig_female.glb` (bare body and head) and `rig_mine.glb` (the first signed-in profile's avatar, in its clothes)
beside its `.xex`, each file's progress and how it went on screen (text the CPU draws into the front buffer, a
5x7 font of our own), then waits for a button. Built 2026-11-08 (first as a PC tool, moved onto the console the
same day):

- **The skeleton**: the 71 bones of 4.4 in the library's order and tree, the pivots `xsdk_avatar_skeleton_build`
  places for the avatar's body (MaleBody: root (0, 0.646, 0.001); FemaleBody: (0, 0.665, 0.004)), every rest
  rotation the identity (no node rotation; the inverse bind matrices a translation back from each pivot). A rotation
  keyed on it is the library's pose rotation as it is.
- **The names**: 360sona's (4.7): humanoid names for the named bones, `shape_NN` for the rest, so that
  `anim_convert.py` matches joints by name once a 3D program has reordered them (by order before that).
- **The avatar**: its parts as the avatar layer loads them (`avatar_outfit_load`: stock parts, item packages, the
  shapes' moved vertices), skinned as the renderer skins them (a mesh naming no bone on the root), the triangles
  other parts hide left out (`avatar_outfit_hidden`, shared with the renderer). Metres, y up, facing +z.
- **Textures** (added 2026-11-08): a glTF material per material of each part, its textures baked into one
  PNG (src/avatar_bake.c, src/avatar_png.c) as the renderer mixes them (3.6): an item's base, its tint mask in its
  custom colours (untinted parts left as their base), its decal by its alpha; the Head's skin and six layers in
  their colours, frame 0 (the neutral face). Every pack material samples base, mask and decal with sets of its own
  (632 of 634; the other 2 have no base), so the bake draws the material's triangles in the base's set (the Head's
  first layer's), 64 to 512 texels a side (the Head 256), and inside each finds the other textures' points by
  interpolating their sets; texels no triangle covers are grown from their neighbours, 8 rings, for filtering across
  seams. DXT1 and DXT5 decoded on the CPU as research/texture.py decodes them; reflection maps (view-dependent) left
  out; sunglasses lenses blended by the base's alpha. PNG with fixed-code deflate (hash-chain matches): 2 to 50% of
  the raw size. A material with nothing to bake (the bare Head) keeps its first colour.
- **Byte order**: glTF is little-endian; on the console each float and 16-bit index is turned round as it goes into
  the binary chunk, the header written byte by byte.
- The files hold the console's meshes: they stay out of the repository, as R1 does.

Checked 2026-11-08 on the PC (`outfit_check <pack> export <out.glb> male|female|stock|<description> [folders]`, the same
code with the console's file reads replaced): the GLB's structure for the bare bodies, the stock avatar and P1's avatar
(accessors in bounds, weights summing to 1, the bind matrices against the joint tree within 2e-6 m, joints matched by
order); C1 hides 2218 of the body's triangles, as on the console (test run 5), 46 left (the hands). A round trip through
Blender 5.1.1 in the background (glTF importer and exporter, defaults, sampled at 30 a second), on the bare body and
again on P1's avatar: seven bones keyed with turns of 15 to 70 degrees about mixed axes, and the hips moved; converted,
every bone of every frame equals Blender's pose within 0.04 degrees (joints matched by name, none left over), the root's
move exactly; the pivot is ours, so the loader moves nothing. Rendered at rest and posed, body and clothes follow their
bones. The byte order: the PC build made to take the big-endian path gives the same file with exactly each float and
index turned round.

**On the console** (test run 7, 2026-11-08): all three files written (87, 91 and 274 KB), P2's avatar a suit (hiding
2230 of the body's triangles, 34 left) and a helmet (moving 180 of the Head's vertices). The bare bodies equal the PC's
exports but for the last bit of some floats (1.2e-7 m at most: the console's fused multiply-adds), joints, weights and
indices identical; every file passes the structural check, and `rig_mine.glb` the Blender round trip (0.04 degrees) and
renders, so the byte order is right.

The textures, checked 2026-11-08 on the PC: the DXT decoder equals research/texture.py byte for byte on the Male Punk
Tee's three textures (DXT1 and DXT5); every PNG of the stock avatar's and P1's exports decodes with zlib, its CRCs
right; Blender shows the stock avatar in its green tee (its mask is empty: untinted), jeans and sneakers, and P1's
in C1, G1's green lenses see-through and the face's eyes, eyebrows and mouth in place; the round trip unchanged
(0.04 degrees). P1's export grew from 314 to 613 KB.

**On the console with textures** (test run 8, 2026-11-08): all three files written (97, 101 and 635 KB; the bare
bodies the PC's sizes to the byte), the display opened for the progress screen; every file passes the structural
check and every PNG decodes; Blender shows P2's suit and helmet in their own colours and detail
and the face in place, and the round trip holds (0.04 degrees).

## 5. Open questions

### 5.1 Stock assets and the description

1. ~~Where the system keeps its asset pack.~~ In package `FFFE07DF00000002` on the system extended partition (4.3).
   Still open: where a console without a hard drive keeps the package (its internal memory unit, presumably). The
   skeleton isn't in it (4.4).
2. The pack format beyond 4.1: the 0xB4 bytes of each record (asset id, type, gender), and whether the system's pack
   is version 1 or 2.
3. ~~The description's setting and layout.~~ Read and decoded (4.6). Still open: what the two floats, the second
   item list and the 16 bytes at 0x380 mean exactly, and how the colours apply (All-Stars Avatar's
   `avatar_colours.py` has a rule).

### 5.2 Model data

0. ~~The body mesh layout.~~ The same as items' (4.1); the failure was a bug in our LZX chunk handling (3.2).
1. The model header words not yet known, and the data after the last mesh (textures? bone bindings?).
2. ~~How a rigid mesh (a hat) is bound to its bone.~~ By the weight and bone bases of its mesh header (3.3). Props (left
   out of the library) bring their own skeleton (block 5) and animation (block 1): the launcher's are 0xB4 and 0x26A70
   bytes, not looked at yet.
3. The position scale per axis (3.3): to settle by drawing an item on the stock body on the console.
4. ~~The texture blocks: format and how they are tied to meshes; the colour rule.~~ Settled (3.4-3.6). Still
   open: the float4 parameters 0x16-0x1B and the Head's 0x0D-0x15, the section after the texture table, ~~which eye
   and mouth frames mean what~~ (settled on the PC, 3.5; the console to confirm), and the default custom colours
   when the description gives none (block 7).
5. ~~Shape overrides (block 4).~~ Settled, both parts (3.7). Still open: the colour tables (block 7): 36 of 39
   have 24 bytes an entry, three (Male Power Tee among them) another layout (3.1).

### 5.3 Rendering

1. Skinning on the GPU: bone matrices as vertex shader constants (xsdk loads constants from the CPU or from memory),
   a xost vertex shader. Bones per draw and the constant budget are to be checked.
2. Whether the packed vertex format is unpacked on the CPU at load time into a plain vertex buffer (simplest, first)
   or read by the vertex fetch directly.
3. Lighting and shading close to the console's look, from captures on the console, not from XDK shaders.
4. Image quality (2026-11-06, seen in test run 5). Compared at 2x and 4x: test
   run 5's screenshot against the system's own render of the same avatar (test run 3's Aurora screenshot, also
   1280x720, the avatar about as tall). The textures are the same data: the costume maps the whole body onto one
   256x256 DXT1 texture (with a 32x32 and a 128x64 one as reflection maps), so on screen it is magnified, not
   shrunk; the hair's three textures are 32x32 flat fills (base, a mask tinting all of it, no decal; no alpha), so
   its look is geometry and light alone. What differs: **edges** (ours step from pixel to pixel; the system's are
   smoothed, multisampled or rendered larger); **shading** (the system's face shows the nose and cheeks, its hair
   and costume have highlights and darker sides; ours is lit per vertex, a direction and an ambient share, and looks
   flat, see 3.); and slightly grainier detail in ours (no mipmaps, every texture one level). xsdk draws into
   targets of 2 and 4 samples (its milestone 32); 720p at 2 samples needs 14.7 MB of eDRAM with depth, more than its
   10 MB, so a title draws the screen in bands (each band recorded once and executed per band, or drawn again).

Learned writing the viewer's shaders for xost (2026-11-04; xsdk's xenos-toolchain/docs/xsc.md has the full list):
a single component of a local (`m.r`) is refused ("indexed access into a local"), so write swizzles (`m.rrr`);
pixel shader inputs must be locations 0, 1, 2, ... with no gaps; a function called more than once is refused
("stored more than once"), so write each step with a variable of its own; push constants start at register 0 in
each stage, the literals the compiler pools follow them, and samplers take fetch slots from 0 by binding.

### 5.5 Animation

1. Whether the stock animations carry face keys (eye, eyebrow and mouth frames over time) in the sections after
   their frame data (4.5), and if so, how to play them. Moved here from milestone 9 when it closed (2026-11-06).
