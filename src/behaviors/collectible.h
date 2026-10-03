// collectible - the player picks it up: add to a variable, play 'collect', disappear.
#ifndef BHV_COLLECTIBLE_H
#define BHV_COLLECTIBLE_H

#include "engine/actor.h"

// Called when the actor touches another one.
void bhv_collectible_on_touch(Actor *self, Actor *other, const void *params);

#endif
