// physics.h - gravity, movement against tiles (solid / one_way / hazard / ladder),
// standing on platforms, and actor-vs-actor contact (which fires on_touch).
#ifndef ENGINE_PHYSICS_H
#define ENGINE_PHYSICS_H

#include "actor.h"

// Move every actor by its speed, resolve collisions, fire on_touch. Once per frame.
void physics_update(void);
// TILE_* flags of the collision-map tile under a world pixel (outside the map sides = solid).
u8 physics_tile_at(s32 px, s32 py);
// True if something can be stood on at this world pixel (solid or one-way tile).
int physics_ground_at(s32 px, s32 py);
// True if the two actors' hitboxes overlap.
int physics_overlap(const Actor *a, const Actor *b);
// True if `top` is landing on `bottom` from above (a stomp).
int physics_is_stomp(const Actor *top, const Actor *bottom);

#endif
