// locked_door - when the player stands against it and the key variable is above 0, it uses
// one key (optional), runs on_open and disappears (playing its 'die' animation as the opening).
// Give the body physics.solid so it blocks the way until then.
#include "locked_door.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"

void bhv_locked_door_update(Actor *a, const void *params) {
    const Params_locked_door *p = params;
    Actor *pl = actor_player();
    if (!pl || p->var < 0 || vars_get(p->var) <= 0) return;
    s32 al, at, ar, ab, pl_l, pl_t, pl_r, pl_b;
    actor_rect(a, &al, &at, &ar, &ab);
    actor_rect(pl, &pl_l, &pl_t, &pl_r, &pl_b);
    int near = pl_l <= ar + p->range && pl_r >= al - p->range && pl_t <= ab + p->range && pl_b >= at - p->range;
    if (!near) return;
    if (p->consume) vars_add(p->var, -1);
    scripts_start(p->on_open, a);
    actor_die(a, 1);
}
