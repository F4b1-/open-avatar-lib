// The store's NPCs (npcs.h).

#include "npcs.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "mesh.h"
#include "xsdk/avatar.h"
#include "xsdk/log.h"

enum {
	MAX_NPCS = 12,
	MAX_EXTRA = 3, // NPCs from description files
};

static const float pi = 3.14159265f;
static const float radius = 0.3f;          // for bumping into
static const float notice = 1.8f;          // how near the player comes before an NPC turns to it
static const float keeper_notice = 2.6f;   // the shopkeeper, behind the counter
static const float turn_rate = 2.5f;       // radians a second
static const float wave_pause = 20.0f;     // seconds between one NPC's waves
static const float shadow_radius = 0.36f;
static const char npc_folder[] = "Avatars";

typedef enum { KEEPER, CHATTER, BROWSER, MANNEQUIN } Kind;

typedef struct {
	const char *title;
	Kind kind;
	float x, z, heading; // heading: 0 faces +z (the store's front), pi/2 faces +x
	int partner;         // the one a chatter talks with, or -1
	const char *stand;
	const XsdkAvatarStockPart *parts;
	uint32_t part_count;
} Spec;

typedef struct {
	XsdkAvatar *avatar;
	Spec spec;
	float heading;
	uint32_t gender;
	float idle_in;
	float wave_pause;
	bool near;      // the player was within notice last update
	bool gesturing; // a one-shot plays; its face goes back to neutral when it ends
	float talk_left;
	float voice; // each one's own rhythm when talking
	int speaker; // for the first of a pair of chatters: which one talks
	float turn_left;
} Npc;

// Faces (xsdk_avatar_set_face).
static const XsdkAvatarFace neutral = { 0, 0, 0, 0, 0 };
static const XsdkAvatarFace happy = { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY };
static const XsdkAvatarFace laughing = { XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYE_LAUGHING,
	XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_MOUTH_LAUGHING };

// The outfits: stock parts by name, each with its colour (xsdk_avatar_load_stock).
#define PARTS(array) array, sizeof array / sizeof array[0]
static const XsdkAvatarStockPart keeper[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 },
	{ "Buzz Cut", 0xFF2A1B10 }, { "Male Punk Tee", 0xFF303030 }, { "Jeans", 0xFF3A4E7A },
	{ "High Top Sneakers", 0xFF202020 }, { "Happy Eyes", 0xFF4A3020 }, { "Studious Eyebrows", 0xFF2A1B10 },
	{ "Grin Mouth", 0xFFB56157 }, { "Jaw Stubble", 0xFF2A1B10 } };
static const XsdkAvatarStockPart chatter_a[] = { { "FemaleBody", 0xFFF1C27D }, { "Head", 0xFFF1C27D },
	{ "Ponytail", 0xFF6B3A1E }, { "Tight Tee", 0xFFE0E0E0 }, { "Skinny Fit Jeans", 0xFF2E4A7A },
	{ "Sports Trainers", 0xFFF0F0F0 }, { "Almond Eyes", 0xFF3B6E3B }, { "Arched Eyebrows", 0xFF4A2A15 },
	{ "Gentle Mouth", 0xFFC0605A } };
static const XsdkAvatarStockPart chatter_b[] = { { "MaleBody", 0xFFE0AC69 }, { "Head", 0xFFE0AC69 },
	{ "Short and Spikey", 0xFFD8B060 }, { "Male Power Tee", 0xFFB03030 }, { "Faded Jeans with Print", 0xFF4A5A80 },
	{ "Grunge Trainers", 0xFF505050 }, { "Kind Eyes", 0xFF3070A0 }, { "Soft Eyebrows", 0xFF8A6A30 },
	{ "Smirk Mouth", 0xFFB56157 } };
static const XsdkAvatarStockPart browser_a[] = { { "MaleBody", 0xFF8D5524 }, { "Head", 0xFF8D5524 },
	{ "Afro", 0xFF1A1010 }, { "Male Bright Sprite Hoodie", 0xFF2E8B3E }, { "Chinos", 0xFF8A7A5A },
	{ "Hiking Shoes", 0xFF5A4030 }, { "Wide Awake Eyes", 0xFF3A2010 }, { "Thick Eyebrows", 0xFF1A1010 },
	{ "Vibrant Mouth", 0xFFA0504A } };
static const XsdkAvatarStockPart browser_b[] = { { "FemaleBody", 0xFFFFDBAC }, { "Head", 0xFFFFDBAC },
	{ "Bob with Fringe", 0xFF2A1A12 }, { "Hacking Jacket", 0xFF6A4A8A }, { "Denim Skirt", 0xFF3A5A9A },
	{ "Ballet Flats", 0xFF202020 }, { "Soft Eyes", 0xFF5A3A20 }, { "Delicate Eyebrows", 0xFF2A1A12 },
	{ "Deep Lips Mouth", 0xFFB04A5A } };
static const XsdkAvatarStockPart browser_c[] = { { "MaleBody", 0xFFDCAD8F }, { "Head", 0xFFDCAD8F },
	{ "Fauxhawk", 0xFF6A3A1A }, { "Tropical Shirt", 0xFF2AA0A0 }, { "Male Cargo Pants", 0xFF4A5A3A },
	{ "Punk Boots", 0xFF202020 }, { "Restful Eyes", 0xFF3A6A9A }, { "Straight Eyebrows", 0xFF6A3A1A },
	{ "Curvy Mouth", 0xFFB56157 }, { "Square Goatee", 0xFF6A3A1A } };
// Mannequins: white, bald, no face.
static const XsdkAvatarStockPart mannequin_m[] = { { "MaleBody", 0xFFF2F2F2 }, { "Head", 0xFFF2F2F2 },
	{ "Bald", 0xFFF2F2F2 }, { "Presenter Jacket", 0xFF2A3A6A }, { "Chinos", 0xFFB0A080 }, { "Formal Shoes", 0xFF3A2A1A } };
static const XsdkAvatarStockPart mannequin_f[] = { { "FemaleBody", 0xFFF2F2F2 }, { "Head", 0xFFF2F2F2 },
	{ "Bald", 0xFFF2F2F2 }, { "Green Full-Zip Hoodie", 0xFF3A9A4A }, { "Polkadot Skirt", 0xFFC03040 },
	{ "Sandals", 0xFFA07040 } };

static const Spec specs[] = {
	{ "shopkeeper", KEEPER, 2.55f, -3.65f, 0.0f, -1, "Animation Generic Stand 1", PARTS(keeper) },
	{ "chatter a", CHATTER, 0.55f, 1.85f, 0.0f, 2, "Animation Generic Stand 2", PARTS(chatter_a) },
	{ "chatter b", CHATTER, 1.35f, 2.35f, 0.0f, 1, "Animation Generic Stand 3", PARTS(chatter_b) },
	{ "browser a", BROWSER, -4.1f, -2.0f, -1.5708f, -1, "Animation Generic Stand 4", PARTS(browser_a) },
	{ "browser b", BROWSER, -1.85f, -0.55f, 3.1416f, -1, "Animation Generic Stand 5", PARTS(browser_b) },
	{ "browser c", BROWSER, 4.05f, -1.0f, 1.5708f, -1, "Animation Generic Stand 6", PARTS(browser_c) },
	{ "mannequin m", MANNEQUIN, -3.9f, 2.5f, 0.35f, -1, NULL, PARTS(mannequin_m) },
	{ "mannequin f", MANNEQUIN, -0.9f, -2.45f, 0.0f, -1, NULL, PARTS(mannequin_f) },
};
enum { SPECS = sizeof specs / sizeof specs[0] };

// Where NPCs from description files stand, browsing.
static const float extra_spots[MAX_EXTRA][3] = { { -3.0f, 1.2f, -2.4f }, { 0.4f, -0.6f, 3.1416f },
	{ 3.4f, -1.9f, 1.5708f } };

static const char *const idles_male[] = { "Animation Male Idle Looks Around", "Animation Male Idle Stretch",
	"Animation Male Idle Shifts Weight", "Animation Male Idle Checks Hand" };
static const char *const idles_female[] = { "Animation Female Idle Check Nails", "Animation Female Idle Looks Around",
	"Animation Female Idle Shifts Weight", "Animation Female Idle Fixes Shoe" };

static Npc npcs[MAX_NPCS];
static uint32_t npc_count;
static XsdkAvatarBonePose mannequin_pose[XSDK_AVATAR_BONES];
static uint32_t random_state = 0x2545F491u;
static float clock_seconds;

static float random_between(float low, float high)
{
	random_state = random_state * 1664525u + 1013904223u;
	return low + (high - low) * (float)(random_state >> 8) / (float)(1u << 24);
}

static float angle_to(float fx, float fz, float tx, float tz)
{
	return atan2f(tx - fx, tz - fz);
}

static float turn_towards(float heading, float target, float step)
{
	float d = target - heading;
	while (d > pi)
		d -= 2.0f * pi;
	while (d < -pi)
		d += 2.0f * pi;
	return heading + (fabsf(d) <= step ? d : (d > 0 ? step : -step));
}

static void place(const Npc *n)
{
	float world[16];
	xsdk_avatar_placement(world, n->spec.x, 0.0f, n->spec.z, n->heading);
	xsdk_avatar_set_transform(n->avatar, world);
}

static bool add(XsdkAvatar *avatar, const Spec *spec)
{
	if (!avatar || npc_count == MAX_NPCS)
		return false;
	Npc *n = &npcs[npc_count++];
	memset(n, 0, sizeof *n);
	n->avatar = avatar;
	n->spec = *spec;
	n->heading = spec->heading;
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar, &info);
	n->gender = info.gender;
	n->idle_in = random_between(3.0f, 12.0f);
	n->voice = random_between(0.0f, 6.0f);
	n->speaker = -1;
	if (spec->kind == MANNEQUIN) {
		xsdk_avatar_set_blinking(avatar, false);
	} else {
		(void)xsdk_avatar_play(avatar, spec->stand ? spec->stand : XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		xsdk_avatar_update(avatar, random_between(0.0f, 4.0f));
	}
	xsdk_log_write("store: %s: %u parts, %u replaced, %u KB GPU", spec->title, (unsigned)info.part_count,
		(unsigned)info.replaced_count, (unsigned)(info.gpu_bytes / 1024));
	place(n);
	return true;
}

// NPCs from description files in the Avatars folder (as samples/viewer loads them), browsing at the spare spots.
static void load_extras(void)
{
	DIR *dir = opendir(npc_folder);
	if (!dir)
		return;
	const struct dirent *entry;
	uint32_t extras = 0;
	while ((entry = readdir(dir)) && extras < MAX_EXTRA) {
		const size_t length = strlen(entry->d_name);
		if (length < 4 || strcasecmp(entry->d_name + length - 4, ".bin") != 0)
			continue;
		char path[300];
		snprintf(path, sizeof path, "%s/%s", npc_folder, entry->d_name);
		static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
		FILE *file = fopen(path, "rb");
		const size_t read = file ? fread(bytes, 1, sizeof bytes, file) : 0;
		if (file)
			(void)fclose(file);
		XsdkAvatar *avatar;
		if (xsdk_avatar_load_description(&avatar, bytes, (uint32_t)read) != XSDK_AVATAR_OK)
			continue;
		const Spec spec = { "description", BROWSER, extra_spots[extras][0], extra_spots[extras][1],
			extra_spots[extras][2], -1, "Animation Generic Stand 7", NULL, 0 };
		if (add(avatar, &spec))
			extras++;
	}
	(void)closedir(dir);
}

uint32_t npcs_load(void)
{
	for (uint32_t i = 0; i < SPECS; i++) {
		XsdkAvatar *avatar;
		const int result = xsdk_avatar_load_stock(&avatar, specs[i].parts, specs[i].part_count);
		if (result != XSDK_AVATAR_OK || !add(avatar, &specs[i])) {
			xsdk_log_write("store: %s: not loaded (%d)", specs[i].title, result);
			npcs[npc_count++] = (Npc){ .spec = specs[i] }; // kept, unloaded, so that partners' numbers hold
		}
	}
	load_extras();
	// Chatters face each other; the first of each pair keeps their turns.
	for (uint32_t i = 0; i < npc_count; i++) {
		Npc *n = &npcs[i];
		if (n->spec.partner >= 0) {
			const Npc *p = &npcs[n->spec.partner];
			n->spec.heading = angle_to(n->spec.x, n->spec.z, p->spec.x, p->spec.z);
			n->heading = n->spec.heading;
			if ((uint32_t)n->spec.partner > i)
				n->speaker = (int)i;
		}
	}
	// Mannequins stand straight, arms a little out from the body.
	xsdk_avatar_pose_reset(mannequin_pose, XSDK_AVATAR_BONES);
	xsdk_avatar_quaternion_from_axis(mannequin_pose[XSDK_AVATAR_BONE_SHOULDER_LEFT].rotation, 0, 0, 1, -1.15f);
	xsdk_avatar_quaternion_from_axis(mannequin_pose[XSDK_AVATAR_BONE_SHOULDER_RIGHT].rotation, 0, 0, 1, 1.15f);
	uint32_t loaded = 0;
	for (uint32_t i = 0; i < npc_count; i++)
		loaded += npcs[i].avatar != NULL;
	return loaded;
}

void npcs_close(void)
{
	for (uint32_t i = 0; i < npc_count; i++)
		xsdk_avatar_close(npcs[i].avatar);
	npc_count = 0;
}

static const char *gendered(const Npc *n, const char *male, const char *female)
{
	return n->gender == XSDK_AVATAR_FEMALE ? female : male;
}

// A one-shot, then back to standing; its face for as long as it plays.
static void gesture(Npc *n, const char *animation, const XsdkAvatarFace *face)
{
	if (xsdk_avatar_play(n->avatar, animation, XSDK_AVATAR_ONCE) != XSDK_AVATAR_OK)
		return;
	(void)xsdk_avatar_queue(n->avatar, n->spec.stand ? n->spec.stand : XSDK_AVATAR_ANIMATION_STAND,
		XSDK_AVATAR_LOOP);
	xsdk_avatar_set_face(n->avatar, face);
	n->gesturing = true;
}

void npcs_answer(int i)
{
	if (i < 0 || (uint32_t)i >= npc_count || !npcs[i].avatar)
		return;
	npcs[i].talk_left = random_between(2.5f, 4.0f);
}

// A pair's turns: one talks a while, then the other; now and then the listener laughs.
static void chat(Npc *first)
{
	if (first->turn_left > 0.0f)
		return;
	Npc *other = &npcs[first->spec.partner];
	if (!other->avatar)
		return;
	const bool first_spoke = first->speaker == (int)(first - npcs);
	Npc *next = first_spoke ? other : first, *listener = first_spoke ? first : other;
	first->speaker = (int)(next - npcs);
	first->turn_left = random_between(2.5f, 4.5f);
	next->talk_left = first->turn_left - 0.4f;
	if (random_between(0.0f, 1.0f) < 0.3f && !listener->gesturing)
		gesture(listener, gendered(listener, "Animation Male Laugh", "Animation Female Laugh"), &laughing);
}

static void update_one(Npc *n, float seconds, float px, float pz)
{
	const float d = hypotf(px - n->spec.x, pz - n->spec.z);
	const bool near = d < (n->spec.kind == KEEPER ? keeper_notice : notice);
	// Turned to the player while it is near, else back as it stood.
	const float target = near ? angle_to(n->spec.x, n->spec.z, px, pz) : n->spec.heading;
	n->heading = turn_towards(n->heading, target, turn_rate * seconds);
	n->wave_pause -= seconds;
	n->idle_in -= seconds;
	n->talk_left -= seconds;
	n->turn_left -= seconds;
	if (n->gesturing && xsdk_avatar_player(n->avatar)->current.loop) {
		n->gesturing = false;
		xsdk_avatar_set_face(n->avatar, &neutral);
	}
	if (near && !n->near && n->wave_pause <= 0.0f && !n->gesturing) {
		gesture(n, XSDK_AVATAR_ANIMATION_WAVE, &happy);
		n->wave_pause = wave_pause;
	} else if (!near && n->idle_in <= 0.0f && !n->gesturing && n->talk_left <= 0.0f) {
		const uint32_t k = (uint32_t)random_between(0.0f, 3.999f);
		gesture(n, gendered(n, idles_male[k], idles_female[k]), &neutral);
		n->idle_in = random_between(8.0f, 16.0f);
	}
	n->near = near;
	if (n->speaker >= 0)
		chat(n);
	const float talking = n->talk_left > 0.0f ? 0.35f + 0.65f * fabsf(sinf(7.3f * clock_seconds + n->voice)) : 0.0f;
	xsdk_avatar_set_talking(n->avatar, talking);
	place(n);
	xsdk_avatar_update(n->avatar, seconds);
}

void npcs_update(float seconds, float px, float pz)
{
	clock_seconds += seconds;
	for (uint32_t i = 0; i < npc_count; i++)
		if (npcs[i].avatar && npcs[i].spec.kind != MANNEQUIN)
			update_one(&npcs[i], seconds, px, pz);
}

void npcs_draw(const float vp[16])
{
	for (uint32_t i = 0; i < npc_count; i++) {
		if (!npcs[i].avatar)
			continue;
		if (npcs[i].spec.kind == MANNEQUIN)
			xsdk_avatar_draw_pose(npcs[i].avatar, mannequin_pose, vp);
		else
			xsdk_avatar_draw(npcs[i].avatar, vp);
	}
}

void npcs_draw_shadows(const float vp[16])
{
	for (uint32_t i = 0; i < npc_count; i++)
		if (npcs[i].avatar)
			mesh_draw_shadow(vp, npcs[i].spec.x, npcs[i].spec.z, shadow_radius);
}

void npcs_collide(float *x, float *z, float r)
{
	for (uint32_t i = 0; i < npc_count; i++) {
		if (!npcs[i].avatar)
			continue;
		const float dx = *x - npcs[i].spec.x, dz = *z - npcs[i].spec.z, d = hypotf(dx, dz), least = r + radius;
		if (d >= least || d < 1e-5f)
			continue;
		*x = npcs[i].spec.x + dx / d * least;
		*z = npcs[i].spec.z + dz / d * least;
	}
}

int npcs_nearest(float x, float z, float reach)
{
	int best = -1;
	float best_d = reach;
	for (uint32_t i = 0; i < npc_count; i++) {
		if (!npcs[i].avatar || npcs[i].spec.kind == MANNEQUIN)
			continue;
		const float d = hypotf(x - npcs[i].spec.x, z - npcs[i].spec.z);
		if (d < best_d) {
			best_d = d;
			best = (int)i;
		}
	}
	return best;
}
