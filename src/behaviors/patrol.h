// patrol - walk back and forth, turning at walls, ledge edges or a distance limit.
#ifndef BHV_PATROL_H
#define BHV_PATROL_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_patrol_init(Actor *self, const void *params);
// Called every tick.
void bhv_patrol_update(Actor *self, const void *params);

#endif
