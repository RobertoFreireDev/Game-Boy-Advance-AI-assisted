// safe_ground - remembers the last safe ground; spikes and pits send the actor back there.
#ifndef BHV_SAFE_GROUND_H
#define BHV_SAFE_GROUND_H

#include "engine/actor.h"

// Called every tick.
void bhv_safe_ground_update(Actor *self, const void *params);

#endif
