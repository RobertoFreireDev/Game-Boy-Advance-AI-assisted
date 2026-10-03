// stompable - defeated when the player lands on top; the player bounces off.
#ifndef BHV_STOMPABLE_H
#define BHV_STOMPABLE_H

#include "engine/actor.h"

// Called when the actor touches another one.
void bhv_stompable_on_touch(Actor *self, Actor *other, const void *params);

#endif
