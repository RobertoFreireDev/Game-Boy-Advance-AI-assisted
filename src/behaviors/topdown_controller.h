// topdown_controller - walk in any direction with the D-pad (no gravity).
#ifndef BHV_TOPDOWN_CONTROLLER_H
#define BHV_TOPDOWN_CONTROLLER_H

#include "engine/actor.h"

// Called every tick.
void bhv_topdown_controller_update(Actor *self, const void *params);

#endif
