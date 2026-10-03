// health - hit points, invincibility after a hit, knockback, death.
#ifndef BHV_HEALTH_H
#define BHV_HEALTH_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_health_init(Actor *self, const void *params);
// Called every tick.
void bhv_health_update(Actor *self, const void *params);
// Hurt an actor that has the health behavior. Returns 1 if the hit counted.
int health_damage(Actor *a, int amount, const Actor *source);

#endif
