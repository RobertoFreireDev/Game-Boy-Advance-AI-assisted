// monster_waves - runs a monster-battle run in waves that come in from the map's edge (see the .c).
#ifndef BHV_MONSTER_WAVES_H
#define BHV_MONSTER_WAVES_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_monster_waves_init(Actor *self, const void *params);
// Called every tick.
void bhv_monster_waves_update(Actor *self, const void *params);

#endif
