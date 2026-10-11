// monster_ai - wander at home, turn hostile near the player, attack, go home (see the .c).
#ifndef BHV_MONSTER_AI_H
#define BHV_MONSTER_AI_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_monster_ai_init(Actor *self, const void *params);
// Called every tick.
void bhv_monster_ai_update(Actor *self, const void *params);
// True while the monster sleeps (a boss waiting for its turn): moves can't hit it yet.
int monster_ai_asleep(const Actor *a);

#endif
