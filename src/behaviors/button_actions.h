// button_actions - run an action list when a button is pressed (e.g. START opens a pause menu).
#ifndef BHV_BUTTON_ACTIONS_H
#define BHV_BUTTON_ACTIONS_H

#include "engine/actor.h"

// Called every tick.
void bhv_button_actions_update(Actor *self, const void *params);

#endif
