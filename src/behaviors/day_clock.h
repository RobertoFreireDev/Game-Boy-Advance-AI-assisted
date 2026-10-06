// day_clock - the in-game clock: minutes, hours and days, with actions every hour and every new day.
#ifndef BHV_DAY_CLOCK_H
#define BHV_DAY_CLOCK_H

#include "engine/actor.h"

// Called every tick.
void bhv_day_clock_update(Actor *self, const void *params);

#endif
