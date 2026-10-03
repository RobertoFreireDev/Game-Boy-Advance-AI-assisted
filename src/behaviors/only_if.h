// only_if - the object exists (or shows) only while a variable check is true.
#ifndef BHV_ONLY_IF_H
#define BHV_ONLY_IF_H

#include "engine/actor.h"

// Called every tick.
void bhv_only_if_update(Actor *self, const void *params);

#endif
