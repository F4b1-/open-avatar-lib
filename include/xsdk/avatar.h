#ifndef XSDK_AVATAR_H
#define XSDK_AVATAR_H

// Xbox 360 avatars for xsdk titles, in three layers (GOALS.md, section 5), from the bottom up:
// - the asset layer: the system's stock pack, item files, descriptions, models, textures, skeletons and animations
//   decoded into plain structs (SPECS.md, sections 3 and 4). Plain C17 that builds for the PC too; it reads through
//   a callback and allocates with malloc;
// - the animation layer: poses, blending, skin matrices and a player that turns animations into poses (PC too);
// - the avatar layer (XsdkAvatar, console only): load an avatar, play animations, draw it in the title's own pass.
// Results are 0 (XSDK_AVATAR_OK) or a negative XSDK_AVATAR_ERROR_*.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Results: 0 for success, else one of these.
enum {
	XSDK_AVATAR_OK = 0,
	XSDK_AVATAR_ERROR_READ = -1,
	XSDK_AVATAR_ERROR_FORMAT = -2,
	XSDK_AVATAR_ERROR_MEMORY = -3,
	XSDK_AVATAR_ERROR_NOT_FOUND = -4,
	XSDK_AVATAR_ERROR_UNSUPPORTED = -5,
	XSDK_AVATAR_ERROR_GPU = -6, // the GPU layer refused: shaders or textures (the log says which)
};

// Reads length bytes at offset of the source into buffer; returns 0, or nonzero if it couldn't read them all.
typedef int (*XsdkAvatarRead)(void *context, uint64_t offset, void *buffer, uint32_t length);

// The stock asset pack, AvatarAssetPack.toc inside the system package FFFE07DF00000002 (an STFS package).
typedef struct XsdkAvatarPack XsdkAvatarPack;

// Opens the package that read reads; keeps read and context until xsdk_avatar_pack_close.
int xsdk_avatar_pack_open(XsdkAvatarPack **pack, XsdkAvatarRead read, void *context);
void xsdk_avatar_pack_close(XsdkAvatarPack *pack);
uint32_t xsdk_avatar_pack_count(const XsdkAvatarPack *pack);
// The record's English name in UTF-8 (ASCII in the stock pack), or "" for an empty record.
const char *xsdk_avatar_pack_name(const XsdkAvatarPack *pack, uint32_t index);
// The record's STRB size in bytes; 0 for an empty record.
uint32_t xsdk_avatar_pack_asset_size(const XsdkAvatarPack *pack, uint32_t index);
// The first record with this English name.
int xsdk_avatar_pack_find(const XsdkAvatarPack *pack, const char *name, uint32_t *index);
// Reads the record's STRB file into a buffer of xsdk_avatar_pack_asset_size bytes.
int xsdk_avatar_pack_read_asset(const XsdkAvatarPack *pack, uint32_t index, void *buffer);

// Reads a file from the root folder of an STFS package (an item package's asset_v2.bin, say) into a malloc'd buffer.
int xsdk_avatar_package_read_file(
	XsdkAvatarRead read, void *context, const char *name, void **data, uint32_t *size_bytes);

// A profile's avatar description (SPECS.md, section 4.6): what XamAvatarGetManifestLocalUser fills in.
enum {
	XSDK_AVATAR_DESCRIPTION_BYTES = 1000,
	XSDK_AVATAR_FEATURES = 15,
	XSDK_AVATAR_ITEMS = 15,
	XSDK_AVATAR_UNDER_ITEMS = 4,
	XSDK_AVATAR_ID_BYTES = 16,
};

// The description's colours, in its order.
enum {
	XSDK_AVATAR_COLOR_SKIN,
	XSDK_AVATAR_COLOR_HAIR,
	XSDK_AVATAR_COLOR_MOUTH,
	XSDK_AVATAR_COLOR_EYES,
	XSDK_AVATAR_COLOR_EYEBROWS,
	XSDK_AVATAR_COLOR_EYE_SHADOW,
	XSDK_AVATAR_COLOR_FACIAL_HAIR,
	XSDK_AVATAR_COLOR_FEATURE_1,
	XSDK_AVATAR_COLOR_FEATURE_2,
	XSDK_AVATAR_COLORS,
};

typedef struct {
	uint8_t id[XSDK_AVATAR_ID_BYTES];
	uint32_t slots;     // component mask: 0x1 head, 0x2 body, 0x4 hair, 0x8 shirt, 0x10 trousers, 0x20 shoes, ...
	uint32_t colors[3]; // custom colours, ARGB; 0 where unused
} XsdkAvatarItem;

typedef struct {
	float height; // inferred meanings (SPECS.md, section 4.6)
	float weight;
	uint32_t feature_count;
	uint8_t features[XSDK_AVATAR_FEATURES][XSDK_AVATAR_ID_BYTES];
	uint32_t colors[XSDK_AVATAR_COLORS]; // ARGB
	uint32_t item_count;
	XsdkAvatarItem items[XSDK_AVATAR_ITEMS];
	// A second list: the outfit under a costume, it appears.
	uint32_t under_count;
	XsdkAvatarItem under[XSDK_AVATAR_UNDER_ITEMS];
} XsdkAvatarDescription;

int xsdk_avatar_description_parse(XsdkAvatarDescription *description, const void *bytes, uint32_t size_bytes);
// Whether the id names a stock asset, and its record in the pack; else it names an item package.
bool xsdk_avatar_id_is_stock(const uint8_t *id, uint32_t *record);
// An item's package file name: its id as 32 upper-case hex digits; title_id gets the title it belongs to.
void xsdk_avatar_id_package_name(const uint8_t *id, char name[33], uint32_t *title_id);
// The stock id of the asset a pack record links to (SPECS.md, section 3.7): for clothes, the body template that hides
// the skin under them (its component mask XSDK_AVATAR_SLOT_TEMPLATE); for hair, its variant under a hat. False if none.
bool xsdk_avatar_pack_link(const XsdkAvatarPack *pack, uint32_t index, uint8_t id[XSDK_AVATAR_ID_BYTES]);

// Component masks: the slots an asset fills (SPECS.md, sections 4.1 and 4.6). A pack record's mask is 32 bits, an
// item's slots in a description its low 16 (costumes fill several: shirt, trousers and shoes, ...).
enum {
	XSDK_AVATAR_SLOT_HEAD = 0x1,
	XSDK_AVATAR_SLOT_BODY = 0x2,
	XSDK_AVATAR_SLOT_HAIR = 0x4,
	XSDK_AVATAR_SLOT_SHIRT = 0x8,
	XSDK_AVATAR_SLOT_TROUSERS = 0x10,
	XSDK_AVATAR_SLOT_SHOES = 0x20,
	XSDK_AVATAR_SLOT_HAT = 0x40,
	XSDK_AVATAR_SLOT_GLOVES = 0x80,
	XSDK_AVATAR_SLOT_GLASSES = 0x100,
	XSDK_AVATAR_SLOT_WRIST = 0x200,
	XSDK_AVATAR_SLOT_EARRINGS = 0x400,
	XSDK_AVATAR_SLOT_RING = 0x800,
	XSDK_AVATAR_SLOT_PROP = 0x1000,           // carried props: left out (SPECS.md, section 6.1)
	XSDK_AVATAR_SLOT_ANIMATION = 0x00400000,  // the stock animations, all 60 and nothing else
	XSDK_AVATAR_SLOT_TEMPLATE = 0x01000000,   // body templates ("... Hiding Template")
};

// Genders, as pack records and stock ids give them.
enum {
	XSDK_AVATAR_MALE = 1,
	XSDK_AVATAR_FEMALE = 2,
	XSDK_AVATAR_EITHER = 3,
};

// The record's component mask (XSDK_AVATAR_SLOT_*) and gender (XSDK_AVATAR_MALE, ...); 0 for an empty record.
uint32_t xsdk_avatar_pack_mask(const XsdkAvatarPack *pack, uint32_t index);
uint32_t xsdk_avatar_pack_gender(const XsdkAvatarPack *pack, uint32_t index);
// The id a description gives the record (SPECS.md, section 4.6): its mask, index and gender, then the stock tail.
void xsdk_avatar_pack_stock_id(const XsdkAvatarPack *pack, uint32_t index, uint8_t id[XSDK_AVATAR_ID_BYTES]);

// A shape override block (SPECS.md, section 3.7): changes to another model, the body's under a garment or the
// Head's for a face shape. Two parts: triangles to leave out, and vertices to replace.
typedef struct {
	uint32_t target_record;      // the stock record of the model it changes: 0 MaleBody, 1 FemaleBody, 2 Head
	uint32_t target_index_bytes; // that model's indices as 16-bit (its header word 4), to check it's the same model
	uint32_t hidden_count;
	uint16_t *hidden; // triangles to leave out: ascending, numbered across the target's meshes in order
	// Vertices to replace, each a whole vertex as a mesh's (XsdkAvatarMesh), by its byte offset into the target's
	// unpacked vertex buffer; xsdk_avatar_shape_apply does the replacing.
	uint32_t override_count;
	uint32_t *override_offsets;
	float *positions; // 3 per override
	float *normals;   // 3 per override
	uint32_t *colors;
	uint8_t *bones;   // 4 per override
	uint8_t *weights; // 4 per override
} XsdkAvatarShape;

// Decodes the shape override block of an STRB file: a template or face shape from the pack, or an item's own
// (marketplace items carry theirs). XSDK_AVATAR_ERROR_NOT_FOUND if the file has none.
int xsdk_avatar_shape_decode(XsdkAvatarShape *shape, const void *strb, uint32_t size_bytes);
void xsdk_avatar_shape_free(XsdkAvatarShape *shape);

// One mesh of a model, decoded: positions in metres, y up, the avatar facing +z (SPECS.md, section 3.3).
typedef struct {
	uint32_t vertex_count;
	float *positions; // 3 per vertex
	float *normals;   // 3 per vertex, unit length
	uint32_t *colors; // 1 per vertex, ARGB
	uint8_t *bones;   // 4 per vertex, bone indices 0-70 (a rigid mesh's all alike: the Head's bone 19, say)
	uint8_t *weights; // 4 per vertex, summing to 255; NULL (and bones NULL) only if the mesh names no bone
	uint32_t uv_sets; // texture coordinate pairs per vertex
	float *uvs;       // 2 * uv_sets per vertex
	uint32_t index_count;
	uint16_t *indices; // triangle list
	uint32_t material; // index into the model's materials: the part the mesh belongs to
} XsdkAvatarMesh;

// A texture (SPECS.md, sections 3.4 and 3.5): DXT blocks as stored, linear (not tiled), rows of the texture's own
// width, frames one after another. Each 16-bit word is byte-swapped (the Xenos 8-in-16 endian mode: set it in the
// texture fetch constant, or swap on the CPU). A GPU copy needs a pitch of at least 128 texels.
enum {
	XSDK_AVATAR_TEXTURE_DXT1 = 0x12, // the Xenos data formats
	XSDK_AVATAR_TEXTURE_DXT5 = 0x14,
};

typedef struct {
	uint32_t format; // XSDK_AVATAR_TEXTURE_*
	uint32_t width;
	uint32_t height;
	uint32_t frame_count; // 5 for eyebrows, 14 for eyes and mouths, else 1
	uint32_t frame_bytes; // width * height / 16 * (8 for DXT1, 16 for DXT5)
	uint8_t *data;        // frame_count * frame_bytes; NULL for an empty slot (the Head's), filled from a face asset
} XsdkAvatarTexture;

// The frames of the face textures (SPECS.md, section 3.5): the same order in every style, read from the pictures.
// The names are XNA Game Studio 4.1's (AvatarEye, AvatarEyebrow, AvatarMouth), which list the same 14, 5 and 14
// expressions without their order. Eyes and eyebrows are drawn per half of the face, the texture's left edge at the
// nose, so LOOK_IN looks towards the nose and LOOK_OUT towards the ear.
enum {
	XSDK_AVATAR_EYE_NEUTRAL,
	XSDK_AVATAR_EYE_SAD,
	XSDK_AVATAR_EYE_ANGRY,
	XSDK_AVATAR_EYE_CONFUSED,
	XSDK_AVATAR_EYE_LAUGHING,
	XSDK_AVATAR_EYE_SHOCKED,
	XSDK_AVATAR_EYE_HAPPY,
	XSDK_AVATAR_EYE_YAWNING,
	XSDK_AVATAR_EYE_BLINK,
	XSDK_AVATAR_EYE_LOOK_UP,
	XSDK_AVATAR_EYE_LOOK_DOWN,
	XSDK_AVATAR_EYE_LOOK_IN,
	XSDK_AVATAR_EYE_LOOK_OUT,
	XSDK_AVATAR_EYE_SLEEPING, // the same picture as BLINK in every style
	XSDK_AVATAR_EYE_FRAMES,
};

enum {
	XSDK_AVATAR_EYEBROW_NEUTRAL,
	XSDK_AVATAR_EYEBROW_SAD,
	XSDK_AVATAR_EYEBROW_ANGRY,
	XSDK_AVATAR_EYEBROW_CONFUSED,
	XSDK_AVATAR_EYEBROW_RAISED,
	XSDK_AVATAR_EYEBROW_FRAMES,
};

// The seven lip-sync shapes' names are matched to the pictures by their look (inferred): the tongue between the
// teeth for "dth", the upper teeth on the lip for "fv", and so on.
enum {
	XSDK_AVATAR_MOUTH_NEUTRAL,
	XSDK_AVATAR_MOUTH_SAD,
	XSDK_AVATAR_MOUTH_ANGRY,
	XSDK_AVATAR_MOUTH_CONFUSED,
	XSDK_AVATAR_MOUTH_LAUGHING,
	XSDK_AVATAR_MOUTH_SHOCKED,
	XSDK_AVATAR_MOUTH_HAPPY,
	XSDK_AVATAR_MOUTH_O,
	XSDK_AVATAR_MOUTH_AI,
	XSDK_AVATAR_MOUTH_L,
	XSDK_AVATAR_MOUTH_FV,
	XSDK_AVATAR_MOUTH_W,
	XSDK_AVATAR_MOUTH_DTH,
	XSDK_AVATAR_MOUTH_EE,
	XSDK_AVATAR_MOUTH_FRAMES,
};

// A material's parameters (SPECS.md, section 3.6): texture bindings and float4 values, by id.
enum {
	XSDK_AVATAR_PARAMETER_BASE = 1,  // the base colour texture
	XSDK_AVATAR_PARAMETER_MASK = 2,  // the tint mask: r, g, b pick custom colours 1-3, a blends them in
	XSDK_AVATAR_PARAMETER_DECAL = 3, // a decal over the result, by its alpha
	XSDK_AVATAR_PARAMETER_REFLECTION = 4, // in place of 3 on earrings and glasses: uv_set 0xFFFF (none), strength
	                                      // in float4 0x19 x (a reflection map, inferred)
	XSDK_AVATAR_PARAMETER_SHINE = 0x19,
	XSDK_AVATAR_NO_UV_SET = 0xFFFF,
};

// A texture binding's address mode (SPECS.md, section 3.6): checked by drawing, clamping the mode-0 face layers
// (eyebrows, eyes, mouth) removes copies of them that wrapping leaves at the temples and cheeks.
enum {
	XSDK_AVATAR_ADDRESS_CLAMP = 0,
	XSDK_AVATAR_ADDRESS_WRAP = 3,
};

typedef struct {
	uint32_t id;
	bool is_texture;
	uint16_t texture; // a texture binding: index into the model's textures
	uint16_t uv_set;  // and the mesh texture coordinate set it samples with
	uint32_t address; // XSDK_AVATAR_ADDRESS_*
	float value[4];   // a float4
} XsdkAvatarParameter;

// A model is a series of parts, each a material and the meshes after it (SPECS.md, section 3.6). The part header's
// first word says how the part is drawn; the meanings are inferred from where each occurs in the pack.
enum {
	XSDK_AVATAR_TECHNIQUE_STANDARD = 0, // bodies, nearly all clothes
	XSDK_AVATAR_TECHNIQUE_HAT_HAIR = 1, // hair variants worn under a hat
	XSDK_AVATAR_TECHNIQUE_SHINY = 2,    // earrings, sunglasses frames, some costumes
	XSDK_AVATAR_TECHNIQUE_SEE_THROUGH = 3, // sunglasses lenses: blended by the base texture's alpha
	XSDK_AVATAR_TECHNIQUE_HEAD = 4,     // the Head: its face layers
};

typedef struct {
	uint32_t technique; // XSDK_AVATAR_TECHNIQUE_*
	uint32_t parameter_count;
	XsdkAvatarParameter *parameters;
} XsdkAvatarMaterial;

typedef struct {
	uint32_t mesh_count;
	XsdkAvatarMesh *meshes;
	uint32_t texture_count;
	XsdkAvatarTexture *textures;
	uint32_t material_count; // one per part
	XsdkAvatarMaterial *materials;
	// The item's colour table (block 7, SPECS.md section 3.1): per entry its three default custom colours, ARGB, for
	// when a description gives none. 0 entries if it has none, or a layout not yet understood (3 in the pack).
	uint32_t color_entry_count;
	uint32_t (*color_entries)[3];
} XsdkAvatarModel;

// Decodes the model block of an STRB file (an item's asset_v2.bin or a pack record): meshes, textures and material.
// A model with no meshes (Bald) decodes to zero meshes, textures and materials. XSDK_AVATAR_ERROR_NOT_FOUND if the
// file has no model block.
int xsdk_avatar_model_decode(XsdkAvatarModel *model, const void *strb, uint32_t size_bytes);
void xsdk_avatar_model_free(XsdkAvatarModel *model);
// The material's parameter with this id, or NULL.
const XsdkAvatarParameter *xsdk_avatar_material_parameter(const XsdkAvatarMaterial *material, uint32_t id);
// Replaces the target model's vertices that a shape overrides (xsdk_avatar_shape_decode), numbered across its meshes:
// an override's byte offset over the target's unpacked vertex size, 28 bytes and 4 per texture coordinate set.
// XSDK_AVATAR_ERROR_FORMAT if the model isn't the shape's target (its index bytes differ).
int xsdk_avatar_shape_apply(const XsdkAvatarShape *shape, XsdkAvatarModel *target);

// Decodes the texture block of an STRB file: a face feature from the pack (eyebrows, eyes, mouth, ...), DXT5.
// XSDK_AVATAR_ERROR_NOT_FOUND if the file has no texture block.
int xsdk_avatar_texture_decode(XsdkAvatarTexture *texture, const void *strb, uint32_t size_bytes);
void xsdk_avatar_texture_free(XsdkAvatarTexture *texture);

// Skeletons (SPECS.md, section 4.4). No asset holds the avatar's: it is a table of this library's, derived from the
// data, with rest positions worked out from a body's skin weights. Props carry their own (block 5).
enum {
	XSDK_AVATAR_BONES = 71,     // the avatar's
	XSDK_AVATAR_MAX_BONES = 72, // room in a skeleton: the avatar's, or a prop's own
	XSDK_AVATAR_NO_PARENT = 0xFF,
};

// The avatar's named bones, numbered as the public XNA Game Studio 4.1 reference numbers them (AvatarBone); the
// unnamed ones are skin helpers, twist bones and finger joints (SPECS.md, section 4.4).
enum {
	XSDK_AVATAR_BONE_ROOT = 0,
	XSDK_AVATAR_BONE_BACK_LOWER = 1,
	XSDK_AVATAR_BONE_HIP_LEFT = 2,
	XSDK_AVATAR_BONE_HIP_RIGHT = 3,
	XSDK_AVATAR_BONE_BACK_UPPER = 5,
	XSDK_AVATAR_BONE_KNEE_LEFT = 6,
	XSDK_AVATAR_BONE_KNEE_RIGHT = 8,
	XSDK_AVATAR_BONE_ANKLE_LEFT = 11,
	XSDK_AVATAR_BONE_COLLAR_LEFT = 12,
	XSDK_AVATAR_BONE_NECK = 14,
	XSDK_AVATAR_BONE_ANKLE_RIGHT = 15,
	XSDK_AVATAR_BONE_COLLAR_RIGHT = 16,
	XSDK_AVATAR_BONE_HEAD = 19,
	XSDK_AVATAR_BONE_SHOULDER_LEFT = 20,
	XSDK_AVATAR_BONE_TOE_LEFT = 21,
	XSDK_AVATAR_BONE_SHOULDER_RIGHT = 22,
	XSDK_AVATAR_BONE_TOE_RIGHT = 23,
	XSDK_AVATAR_BONE_ELBOW_LEFT = 25,
	XSDK_AVATAR_BONE_ELBOW_RIGHT = 28,
	XSDK_AVATAR_BONE_WRIST_LEFT = 33,
	XSDK_AVATAR_BONE_WRIST_RIGHT = 36,
	XSDK_AVATAR_BONE_PROP_LEFT = 41,
	XSDK_AVATAR_BONE_SPECIAL_LEFT = 42,
	XSDK_AVATAR_BONE_PROP_RIGHT = 48,
	XSDK_AVATAR_BONE_SPECIAL_RIGHT = 49,
};

typedef struct {
	uint32_t bone_count;
	uint8_t parents[XSDK_AVATAR_MAX_BONES]; // XSDK_AVATAR_NO_PARENT for the root; a parent's index is below its child's
	float positions[XSDK_AVATAR_MAX_BONES][3]; // rest position, in the meshes' space
	float rotations[XSDK_AVATAR_MAX_BONES][4]; // rest orientation in the meshes' space, a quaternion x, y, z, w; the
	                                           // avatar's bones all keep the world axes (identity)
} XsdkAvatarSkeleton;

// The avatar's skeleton for a body: MaleBody or FemaleBody (pack records 0 and 1), whose skin weights place the
// joints. XSDK_AVATAR_ERROR_FORMAT if the model has no skinned meshes.
int xsdk_avatar_skeleton_build(XsdkAvatarSkeleton *skeleton, const XsdkAvatarModel *body);
// A prop's own skeleton, the skeleton block of its STRB file. XSDK_AVATAR_ERROR_NOT_FOUND if it has none.
int xsdk_avatar_skeleton_decode(XsdkAvatarSkeleton *skeleton, const void *strb, uint32_t size_bytes);

// One bone of a pose: a change from the rest pose in the bone's own rest frame (SPECS.md, section 4.5). The bone's
// transform is its rest transform relative to its parent's, then translation, then rotation, then scale.
typedef struct {
	float translation[3];
	float rotation[4]; // quaternion x, y, z, w
	float scale[3];
} XsdkAvatarBonePose;

// The rest pose: no translation, the identity rotation, scale 1.
void xsdk_avatar_pose_reset(XsdkAvatarBonePose *pose, uint32_t bone_count);
// The rotation an animation stores, a quaternion's logarithm (the axis times half the angle), as a quaternion.
void xsdk_avatar_quaternion_from_log(float quaternion[4], const float log[3]);
// A rotation by angle radians about an axis (x, y, z, of any length), as a quaternion.
void xsdk_avatar_quaternion_from_axis(float quaternion[4], float x, float y, float z, float angle);
// Per bone, the matrix that takes a rest-pose vertex in the meshes' space to its place in the pose: 3 rows of 4,
// row-major, [rotation | translation]. A vertex goes to the sum over its four bones of weight / 255 times its bone's
// matrix times (x, y, z, 1); a normal the same with 0 for 1.
void xsdk_avatar_skin_matrices(
	const XsdkAvatarSkeleton *skeleton, const XsdkAvatarBonePose *pose, float (*matrices)[12]);
// Blends two poses bone by bone: weight 0 gives a, 1 gives b; translations and scales linearly, rotations along the
// shorter way. out may be a or b.
void xsdk_avatar_pose_blend(XsdkAvatarBonePose *out, const XsdkAvatarBonePose *a, const XsdkAvatarBonePose *b,
	float weight, uint32_t bone_count);

// An animation (SPECS.md, section 4.5): the animation block of a stock pack record ("Animation Generic Wave", ...),
// decoded into one bone pose per bone and frame, as changes from the rest pose (XsdkAvatarBonePose). The avatar's
// bones only: a prop's own part (the sleep animations' sheep) is left out. 40 bytes per bone and frame: about 300 KB
// for a five-second animation.
typedef struct {
	uint32_t frame_count;
	float frames_per_second; // about 20
	uint32_t bone_count;     // XSDK_AVATAR_BONES
	// Frame f's pose of bone b is at f * bone_count + b. Consecutive frames' rotations keep the same sign, so that
	// interpolating between them takes the shorter way.
	XsdkAvatarBonePose *poses;
} XsdkAvatarAnimation;

// Decodes the animation block of an STRB file. XSDK_AVATAR_ERROR_NOT_FOUND if it has none.
int xsdk_avatar_animation_decode(XsdkAvatarAnimation *animation, const void *strb, uint32_t size_bytes);
void xsdk_avatar_animation_free(XsdkAvatarAnimation *animation);
// Its length in seconds, (frame_count - 1) / frames_per_second: in a looping animation the last frame repeats the
// first, so playing it in a loop wraps at this length.
float xsdk_avatar_animation_length(const XsdkAvatarAnimation *animation);
// The pose at a time in seconds, interpolated between the two frames around it: wrapped into the length if loop,
// else held at the first or last frame outside it. pose gets bone_count bones.
void xsdk_avatar_animation_sample(
	const XsdkAvatarAnimation *animation, float seconds, bool loop, XsdkAvatarBonePose *pose);

// A walk made in code, with no asset behind it (the stock animations have none; SPECS.md, section 4.5): one loop of
// two steps. Each foot follows a gait (on the floor, moving back at the walking speed so that it doesn't slide;
// landing on the heel, rolling off the toes, swinging forward clear of the floor) and the legs are solved to reach
// it; the body sinks as the legs need, sways over the standing foot, the pelvis turns and dips, the chest and head
// turn back, the arms swing against the legs. The upper body comes from base, a standing pose of XSDK_AVATAR_BONES
// bones ("Animation Generic Stand 0" sampled at 0, say), made symmetric; or from the rest pose with the arms lowered
// if base is NULL. The legs always start from the rest pose. The skeleton (or NULL) gives the legs' lengths; speed
// (or NULL) gets the metres a second its steps cover, for playing it at the pace the avatar moves
// (XsdkAvatarPlayer.speed = how fast it moves / speed). Free it with xsdk_avatar_animation_free. Until the library's
// own authored walk (milestone 11).
int xsdk_avatar_animation_walk(XsdkAvatarAnimation *walk, const XsdkAvatarBonePose *base,
	const XsdkAvatarSkeleton *skeleton, float *speed);

// How an animation plays.
enum {
	XSDK_AVATAR_ONCE = 0, // to its end, then holds its last frame (xsdk_avatar_player_finished turns true)
	XSDK_AVATAR_LOOP = 1,
};

// An animation playing: which, how far in, and whether it loops.
typedef struct {
	const XsdkAvatarAnimation *animation; // NULL for none
	float time;                           // seconds
	bool loop;
} XsdkAvatarTrack;

// A player turns animations into poses: one plays, cross-faded from the one before, and one more may wait in a
// queue. It only reads the animations, which outlive it; one player can pose any number of avatars. Set speed and
// fade_seconds freely.
typedef struct {
	XsdkAvatarTrack current;
	XsdkAvatarTrack previous; // fading out while fade < fade_length
	float fade;
	float fade_length;
	XsdkAvatarTrack next; // queued, or animation NULL: starts when current.time reaches next_at
	float next_at;
	float speed;        // 1 after init; 0 pauses
	float fade_seconds; // the cross-fade of play and queue: 0.3 after init, 0 cuts
} XsdkAvatarPlayer;

// Nothing playing (the rest pose), speed 1, cross-fades of 0.3 seconds.
void xsdk_avatar_player_init(XsdkAvatarPlayer *player);
// Plays the animation from its start now, cross-faded from what played (at once if nothing did). A fade still
// running is cut short; anything queued is dropped.
void xsdk_avatar_player_play(XsdkAvatarPlayer *player, const XsdkAvatarAnimation *animation, uint32_t mode);
// Plays it after the current one, cross-faded into its end: after a one-shot's last frame, or after the loop's
// current round. Replaces anything queued before; plays at once if nothing plays.
void xsdk_avatar_player_queue(XsdkAvatarPlayer *player, const XsdkAvatarAnimation *animation, uint32_t mode);
// Moves time on by seconds times speed, and starts what is queued when its time comes.
void xsdk_avatar_player_update(XsdkAvatarPlayer *player, float seconds);
// Whether a one-shot has reached its end with nothing queued after it; a loop never finishes.
bool xsdk_avatar_player_finished(const XsdkAvatarPlayer *player);
// The pose now: XSDK_AVATAR_BONES bones.
void xsdk_avatar_player_pose(const XsdkAvatarPlayer *player, XsdkAvatarBonePose *pose);

// ---- The avatar layer (console only) ----
//
// An avatar loaded from the console's own files, ready to draw:
//
//     XsdkAvatar *avatar;
//     xsdk_avatar_load_profile(&avatar, 0);
//     xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
//     each frame, in the title's pass: xsdk_avatar_update(avatar, dt); xsdk_avatar_draw(avatar, view_projection);
//     after a fence past the last frame that drew it: xsdk_avatar_close(avatar);
//
// Loads go after xsdk_gpu_open and are synchronous: they read the stock pack and item packages, decode, and upload
// to GPU memory (tens of milliseconds an avatar). The first load opens the stock pack and loads the shaders, which
// stay until xsdk_avatar_shutdown. One thread at a time.
//
// Matrices are 16 floats, column-major, for column vectors (p' = M p), as GLSL has them. The avatar's own space is
// metres, y up, facing +z, its feet at y 0.
//
// Drawing goes into whatever the title has open: its target, its depth buffer and its pass, or a command buffer
// being recorded (whose constants are those of the moment it was recorded). It never sets targets, clears,
// resolves, presents or waits. A draw changes: the shaders; vertex constants 0-222 and pixel constants 0-29; vertex
// stream 0; texture fetch slots 0-5; the blend state, left as no blending. Depth, culling, the viewport and the
// target's samples are the title's: a multisampled target drawn in bands works (samples/viewer). Lighting reads the
// camera's axes from view_projection, so a title that draws in bands moves the viewport, not the projection.

typedef struct XsdkAvatar XsdkAvatar;

// The common stock animations, by their pack names; xsdk_avatar_stock_animation_name lists all of them.
#define XSDK_AVATAR_ANIMATION_STAND "Animation Generic Stand 0"
#define XSDK_AVATAR_ANIMATION_WAVE "Animation Generic Wave"
#define XSDK_AVATAR_ANIMATION_CLAP "Animation Generic Clap"
#define XSDK_AVATAR_ANIMATION_CELEBRATE "Animation Generic Celebration"

// A stock part by its pack name ("MaleBody", "Head", "Jeans", "Happy Eyes", ...) and its colour, ARGB: the skin for
// a body or the head, the hair colour for hair, the colour of eyes, eyebrows, a mouth, facial hair or eye shadow,
// else all three of its custom colours. Face features (eyes, eyebrows, mouth, facial hair, nose, chin, ears, ...)
// give the Head its face; without them it has none.
typedef struct {
	const char *name;
	uint32_t argb;
} XsdkAvatarStockPart;

// What an avatar is made of and what it takes.
typedef struct {
	uint32_t gender;      // XSDK_AVATAR_MALE or XSDK_AVATAR_FEMALE, by its body
	uint32_t part_count;  // the models drawn
	uint32_t draw_count;  // draws a frame: one per material of each model
	// Items the console doesn't have, each replaced by stock parts of the same kind (hair, shirt, trousers, shoes;
	// a costume by all it covers), and items left out: props, and missing accessories, which have no stand-in.
	uint32_t replaced_count;
	uint32_t left_out_count;
	float height;       // the top of the bind pose, in metres
	uint32_t gpu_bytes; // vertices, indices and textures in GPU memory
	uint32_t cpu_bytes; // the avatar's own heap memory
} XsdkAvatarInfo;

// The avatar of the profile signed in at user_index (0-3): XSDK_AVATAR_ERROR_NOT_FOUND if no one is signed in there
// or the profile has no avatar.
int xsdk_avatar_load_profile(XsdkAvatar **avatar, uint32_t user_index);
// The avatar a saved description gives (XSDK_AVATAR_DESCRIPTION_BYTES): made on a console, or by a web editor such
// as 360sona. How NPCs are shipped.
int xsdk_avatar_load_description(XsdkAvatar **avatar, const void *bytes, uint32_t size_bytes);
// An avatar of stock parts: a body (MaleBody or FemaleBody), "Head", hair, clothes, and face features for a face (a
// plain one without). Up to XSDK_AVATAR_ITEMS items and XSDK_AVATAR_FEATURES features.
// XSDK_AVATAR_ERROR_NOT_FOUND if the pack has no part of a name given.
int xsdk_avatar_load_stock(XsdkAvatar **avatar, const XsdkAvatarStockPart *parts, uint32_t count);
void xsdk_avatar_info(const XsdkAvatar *avatar, XsdkAvatarInfo *info);
// Frees the avatar, after a fence past the last frame that drew it. NULL is ignored.
void xsdk_avatar_close(XsdkAvatar *avatar);

// Plays a stock animation by name (XSDK_AVATAR_ANIMATION_STAND, ...) on the avatar's own player, as
// xsdk_avatar_player_play does. XSDK_AVATAR_ERROR_NOT_FOUND if there is no such animation.
int xsdk_avatar_play(XsdkAvatar *avatar, const char *animation, uint32_t mode);
// Queues one after the current one, as xsdk_avatar_player_queue does.
int xsdk_avatar_queue(XsdkAvatar *avatar, const char *animation, uint32_t mode);
// Whether its one-shot has ended with nothing queued.
bool xsdk_avatar_finished(const XsdkAvatar *avatar);
// Its own player: to set speed or fade_seconds, or to play an animation of the title's.
XsdkAvatarPlayer *xsdk_avatar_player(XsdkAvatar *avatar);
// Moves its player on by seconds and poses it; moves its blinking and talking on.
void xsdk_avatar_update(XsdkAvatar *avatar, float seconds);

// A face: the frame each face texture shows (XSDK_AVATAR_EYE_*, _EYEBROW_*, _MOUTH_*). Left and right are the
// avatar's own.
typedef struct {
	uint8_t left_eye;
	uint8_t right_eye;
	uint8_t left_eyebrow;
	uint8_t right_eyebrow;
	uint8_t mouth;
} XsdkAvatarFace;

// The face it shows when not blinking or talking: all neutral after loading. To look to its own left, the left eye
// looks out and the right eye in.
void xsdk_avatar_set_face(XsdkAvatar *avatar, const XsdkAvatarFace *face);
// Blinking on its own, every two to six seconds: on after loading.
void xsdk_avatar_set_blinking(XsdkAvatar *avatar, bool blinking);
// Talking, from 0 (silent: the face's own mouth) to 1 (loud: the widest shapes): the mouth moves through lip-sync
// shapes as open as the level, a new one every 0.1 s. Send it each frame from a voice's loudness, or hold it while a
// line of dialogue lasts.
void xsdk_avatar_set_talking(XsdkAvatar *avatar, float level);

// Where it stands: its world matrix (the identity after loading).
void xsdk_avatar_set_transform(XsdkAvatar *avatar, const float world[16]);
// Draws it in the pose of its last update, through view_projection.
void xsdk_avatar_draw(const XsdkAvatar *avatar, const float view_projection[16]);
// Draws it in a pose of the caller's (XSDK_AVATAR_BONES bones): from a player of its own, sampled, blended, or set
// bone by bone.
void xsdk_avatar_draw_pose(const XsdkAvatar *avatar, const XsdkAvatarBonePose *pose, const float view_projection[16]);
// Its skeleton, built from its body (SPECS.md, section 4.4).
const XsdkAvatarSkeleton *xsdk_avatar_skeleton(const XsdkAvatar *avatar);

// The light every avatar is drawn with, per pixel: one light, an ambient blended from the ground's colour (surfaces
// facing down) to the sky's (facing up), and a highlight. Colours are linear RGB, 0-1 (more brightens).
typedef struct {
	float direction[3]; // towards the light, in world space; any length
	float color[3];
	float sky[3];
	float ground[3];
	float wrap;      // how far the light reaches round to the side turned away: 0 none, 1 half way
	float highlight; // the highlight's strength, 0 for none; it shows towards the camera
} XsdkAvatarLight;

// The light avatars have until a title sets one: from the upper left, in front.
void xsdk_avatar_default_light(XsdkAvatarLight *light);
void xsdk_avatar_set_light(const XsdkAvatarLight *light);

// A stock animation by name, decoded on first use and kept, shared by every avatar and player, until
// xsdk_avatar_shutdown.
int xsdk_avatar_stock_animation(const char *name, const XsdkAvatarAnimation **animation);
// The stock animations' names, in pack order: 60 on the consoles seen. 0 if the stock pack can't be read.
uint32_t xsdk_avatar_stock_animation_count(void);
const char *xsdk_avatar_stock_animation_name(uint32_t index);

// Frees the stock animations, closes the stock pack and frees the shaders; after every avatar is closed. A later
// load starts afresh.
void xsdk_avatar_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
