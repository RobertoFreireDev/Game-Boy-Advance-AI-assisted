// interact - the player can use this object with the A button (see interactor).
#ifndef BHV_INTERACT_H
#define BHV_INTERACT_H

#include "engine/actor.h"

// The interactable whose area the box (l, t, r, b) touches nearest to (cx, cy), or NULL.
Actor *interact_find(const Actor *user, s32 l, s32 t, s32 r, s32 b, s32 cx, s32 cy);
// Run the object's on_press actions, used by `user`.
void interact_use(Actor *target, Actor *user);

#endif
