// level_up - experience levels that run actions on every level gained.
#ifndef BHV_LEVEL_UP_H
#define BHV_LEVEL_UP_H

#include "engine/actor.h"

// Called every tick.
void bhv_level_up_update(Actor *self, const void *params);

#endif
