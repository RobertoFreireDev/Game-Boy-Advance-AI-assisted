// free_spot - when it appears with its hitbox on a solid tile, the actor moves to the nearest
// spot clear of solid tiles (a monster's drop in an arena's dark edge lands on the floor).
#ifndef BHV_FREE_SPOT_H
#define BHV_FREE_SPOT_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_free_spot_init(Actor *self, const void *params);

#endif
