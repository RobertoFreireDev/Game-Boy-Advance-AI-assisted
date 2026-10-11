// drop_in - fall in from above and bounce (itself or a background layer). See the .c.
#ifndef BHV_DROP_IN_H
#define BHV_DROP_IN_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_drop_in_init(Actor *self, const void *params);
// Called every tick.
void bhv_drop_in_update(Actor *self, const void *params);

#endif
