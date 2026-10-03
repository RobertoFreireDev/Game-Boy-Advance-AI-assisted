// swarm - horde movement: straight at the player, spreading out from neighbours.
#ifndef BHV_SWARM_H
#define BHV_SWARM_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_swarm_init(Actor *self, const void *params);
// Called every tick.
void bhv_swarm_update(Actor *self, const void *params);

#endif
