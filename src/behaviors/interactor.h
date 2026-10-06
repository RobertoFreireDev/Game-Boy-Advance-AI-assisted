// interactor - the player's 'use' button: runs the interact actions of the thing in front.
#ifndef BHV_INTERACTOR_H
#define BHV_INTERACTOR_H

#include "engine/actor.h"

// Called every tick.
void bhv_interactor_update(Actor *self, const void *params);

#endif
