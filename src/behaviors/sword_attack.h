// sword_attack - press a button to swing a sword in front of the actor (top-down games).
#ifndef BHV_SWORD_ATTACK_H
#define BHV_SWORD_ATTACK_H

#include "engine/actor.h"

// Called every tick.
void bhv_sword_attack_update(Actor *self, const void *params);

#endif
