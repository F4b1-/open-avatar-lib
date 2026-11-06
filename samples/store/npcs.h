#ifndef STORE_NPCS_H
#define STORE_NPCS_H

// The store's other avatars: the shopkeeper behind the counter, two shoppers chatting, three browsing, two
// mannequins showing outfits, and any NPCs from description files in the Avatars folder beside the title. Made of
// stock parts by name (xsdk_avatar_load_stock), with faces. They idle, chat in turns, turn to the player when it
// comes close and wave.

#include <stdbool.h>
#include <stdint.h>

// Loads them; logs what each costs. Returns how many loaded.
uint32_t npcs_load(void);
void npcs_close(void);
// Moves their behaviour on; px, pz where the player stands.
void npcs_update(float seconds, float px, float pz);
void npcs_draw(const float vp[16]);
void npcs_draw_shadows(const float vp[16]);
// Moves a circle of radius at (x, z) out of the NPCs.
void npcs_collide(float *x, float *z, float radius);
// The NPC nearest to (x, z) within reach metres, for the player's talking; -1 for none.
int npcs_nearest(float x, float z, float reach);
// Has NPC n answer: it talks for a while, then may laugh.
void npcs_answer(int n);

#endif
