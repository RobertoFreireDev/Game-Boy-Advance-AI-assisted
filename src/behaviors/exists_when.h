// exists_when - the actor only exists while a game variable is inside a range.
#ifndef BHV_EXISTS_WHEN_H
#define BHV_EXISTS_WHEN_H

#include "engine/actor.h"

// Called when the actor is created: removes it at once if the variable is out of range.
void bhv_exists_when_init(Actor *self, const void *params);
// Called every tick: with 'live', removes it as soon as the variable leaves the range.
void bhv_exists_when_update(Actor *self, const void *params);

#endif
