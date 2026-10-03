// platformer_controller - side-view running, jumping (variable height) and ladder climbing.
#ifndef BHV_PLATFORMER_CONTROLLER_H
#define BHV_PLATFORMER_CONTROLLER_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_platformer_controller_init(Actor *self, const void *params);
// Called every tick.
void bhv_platformer_controller_update(Actor *self, const void *params);

#endif
