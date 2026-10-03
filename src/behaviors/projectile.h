// projectile - a launched shot that hurts the enemies it touches.
#ifndef BHV_PROJECTILE_H
#define BHV_PROJECTILE_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_projectile_init(Actor *self, const void *params);
// Called every tick.
void bhv_projectile_update(Actor *self, const void *params);
// Called when the actor touches another one.
void bhv_projectile_on_touch(Actor *self, Actor *other, const void *params);
// Send a freshly spawned shot on its way: direction (dx, dy) is a unit vector in fixed point,
// angle (degrees) is the starting point on the circle for orbiting shots.
void projectile_launch(Actor *shot, const Actor *owner, fixed dx, fixed dy, s32 angle, s32 bonus_damage);

#endif
