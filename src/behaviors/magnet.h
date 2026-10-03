// magnet - a pickup pulled to the player when the player comes close.
#ifndef BHV_MAGNET_H
#define BHV_MAGNET_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_magnet_init(Actor *self, const void *params);
// Called every tick.
void bhv_magnet_update(Actor *self, const void *params);

#endif
