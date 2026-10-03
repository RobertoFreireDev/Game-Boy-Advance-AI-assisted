// trigger_zone - run actions when the player enters or leaves the zone.
#ifndef BHV_TRIGGER_ZONE_H
#define BHV_TRIGGER_ZONE_H

#include "engine/actor.h"

// Called every tick.
void bhv_trigger_zone_update(Actor *self, const void *params);
// Called when the actor touches another one.
void bhv_trigger_zone_on_touch(Actor *self, Actor *other, const void *params);

#endif
