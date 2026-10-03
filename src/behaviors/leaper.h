// leaper - walks toward the player, crouches, then leaps at them and lands with a thud.
#ifndef BHV_LEAPER_H
#define BHV_LEAPER_H

#include "engine/actor.h"

// Called every tick.
void bhv_leaper_update(Actor *self, const void *params);

#endif
