// actions.h - action list interpreter. A running list is a "script": it runs actions in
// order and pauses on wait / fade_in / fade_out, then continues on later frames.
#ifndef ENGINE_ACTIONS_H
#define ENGINE_ACTIONS_H

#include "data.h"

struct Actor;

// Stop every running script (scene change).
void scripts_clear(void);
// Start running an action list now. `self` is the actor that owns it (or NULL).
void scripts_start(ActionList list, struct Actor *self);
// Continue every waiting script. Call once per frame.
void scripts_update(void);
// An actor is being removed: scripts that belong to it keep running without a 'self'.
void scripts_forget_actor(struct Actor *a);

#endif
