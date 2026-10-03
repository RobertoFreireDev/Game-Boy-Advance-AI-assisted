// nail_attack - side-view melee: forward, up and down slashes; down slashes bounce (pogo).
#ifndef BHV_NAIL_ATTACK_H
#define BHV_NAIL_ATTACK_H

#include "engine/actor.h"

// Called every tick.
void bhv_nail_attack_update(Actor *self, const void *params);

#endif
