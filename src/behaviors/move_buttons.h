// move_buttons - the player's 3 moves on A, B and hold-A, with charge timers (see the .c).
#ifndef BHV_MOVE_BUTTONS_H
#define BHV_MOVE_BUTTONS_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_move_buttons_init(Actor *self, const void *params);
// Called every tick.
void bhv_move_buttons_update(Actor *self, const void *params);

#endif
