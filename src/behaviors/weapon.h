// weapon - auto-firing weapon that launches projectile objects on its own.
#ifndef BHV_WEAPON_H
#define BHV_WEAPON_H

#include "engine/actor.h"

// Called every tick.
void bhv_weapon_update(Actor *self, const void *params);

#endif
