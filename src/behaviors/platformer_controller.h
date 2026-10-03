// platformer_controller - side-view running, jumping (variable height) and ladder climbing,
// plus dash, wall jump and double jump when their variables unlock them.
#ifndef BHV_PLATFORMER_CONTROLLER_H
#define BHV_PLATFORMER_CONTROLLER_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_platformer_controller_init(Actor *self, const void *params);
// Called every tick.
void bhv_platformer_controller_update(Actor *self, const void *params);
// Give back the double jump and the air dash (a pogo bounce off an enemy does this).
void platformer_refresh_air(Actor *a);
// True while the actor is in the middle of a dash.
int platformer_dashing(Actor *a);

#endif
