// random_spot - when it appears, the actor moves to a spot picked at random: where it was
// placed or one of its other spots (floor items lie somewhere else every run).
#ifndef BHV_RANDOM_SPOT_H
#define BHV_RANDOM_SPOT_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_random_spot_init(Actor *self, const void *params);

#endif
