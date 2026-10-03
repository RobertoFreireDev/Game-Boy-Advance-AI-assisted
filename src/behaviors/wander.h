// wander - walk around at random in four directions (top-down games).
#ifndef BHV_WANDER_H
#define BHV_WANDER_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_wander_init(Actor *self, const void *params);
// Called every tick.
void bhv_wander_update(Actor *self, const void *params);

#endif
