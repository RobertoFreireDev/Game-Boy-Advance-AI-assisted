// chase_player - move toward the player while the player is within range.
#ifndef BHV_CHASE_PLAYER_H
#define BHV_CHASE_PLAYER_H

#include "engine/actor.h"

// Called every tick.
void bhv_chase_player_update(Actor *self, const void *params);

#endif
