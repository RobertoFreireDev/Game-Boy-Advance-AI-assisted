// locked_door - a door that opens when the player walks up to it holding a key.
#ifndef BHV_LOCKED_DOOR_H
#define BHV_LOCKED_DOOR_H

#include "engine/actor.h"

// Called every tick.
void bhv_locked_door_update(Actor *self, const void *params);

#endif
