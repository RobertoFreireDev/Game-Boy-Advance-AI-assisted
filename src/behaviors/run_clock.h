// run_clock - counts play time in seconds (and mm:ss).
#ifndef BHV_RUN_CLOCK_H
#define BHV_RUN_CLOCK_H

#include "engine/actor.h"

// Called every tick.
void bhv_run_clock_update(Actor *self, const void *params);

#endif
