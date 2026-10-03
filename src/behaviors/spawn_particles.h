// spawn_particles - emit particles always, on landing, on jumping or while running.
#ifndef BHV_SPAWN_PARTICLES_H
#define BHV_SPAWN_PARTICLES_H

#include "engine/actor.h"

// Called every tick.
void bhv_spawn_particles_update(Actor *self, const void *params);

#endif
