// entry_point - where the player appears when a scene starts, picked by a variable.
#ifndef BHV_ENTRY_POINT_H
#define BHV_ENTRY_POINT_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_entry_point_init(Actor *self, const void *params);
// Called every tick.
void bhv_entry_point_update(Actor *self, const void *params);

#endif
