// follow_path - travel along points (relative to the start), pausing at each one.
#ifndef BHV_FOLLOW_PATH_H
#define BHV_FOLLOW_PATH_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_follow_path_init(Actor *self, const void *params);
// Called every tick.
void bhv_follow_path_update(Actor *self, const void *params);

#endif
