// shooter - every few ticks fires an object at the player.
#ifndef BHV_SHOOTER_H
#define BHV_SHOOTER_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_shooter_init(Actor *self, const void *params);
// Called every tick.
void bhv_shooter_update(Actor *self, const void *params);

#endif
