// aura - damaging field around its owner, pulsing every few ticks.
#ifndef BHV_AURA_H
#define BHV_AURA_H

#include "engine/actor.h"

// Called every tick.
void bhv_aura_update(Actor *self, const void *params);

#endif
