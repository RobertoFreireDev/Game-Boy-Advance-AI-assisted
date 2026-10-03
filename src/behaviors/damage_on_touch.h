// damage_on_touch - hurt whoever touches it (unless they stomp it and it is stompable).
#ifndef BHV_DAMAGE_ON_TOUCH_H
#define BHV_DAMAGE_ON_TOUCH_H

#include "engine/actor.h"

// Called when the actor touches another one.
void bhv_damage_on_touch_on_touch(Actor *self, Actor *other, const void *params);

#endif
