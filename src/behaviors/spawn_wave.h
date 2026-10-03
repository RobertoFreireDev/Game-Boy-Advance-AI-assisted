// spawn_wave - spawns enemies off screen during a time window of the run.
#ifndef BHV_SPAWN_WAVE_H
#define BHV_SPAWN_WAVE_H

#include "engine/actor.h"

// Called every tick.
void bhv_spawn_wave_update(Actor *self, const void *params);

#endif
