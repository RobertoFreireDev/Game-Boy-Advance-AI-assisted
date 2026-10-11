// species_select - the monster picker: a bobbing portrait, Left / Right to browse (see the .c).
#ifndef BHV_SPECIES_SELECT_H
#define BHV_SPECIES_SELECT_H

#include "engine/actor.h"

// Called once when the actor appears.
void bhv_species_select_init(Actor *self, const void *params);
// Called every tick.
void bhv_species_select_update(Actor *self, const void *params);

#endif
