// exists_when - the actor only exists while a game variable is inside a range: fruit still on
// its tree, a fossil not dug up today, a butterfly that only comes out by day. min > max wraps
// around (hours 19 to 4 = night).
#include "exists_when.h"
#include "behavior_params.h"
#include "engine/vars.h"

static int in_range(const Params_exists_when *p) {
    s32 v = vars_get(p->var);
    if (p->min <= p->max) return v >= p->min && v <= p->max;
    return v >= p->min || v <= p->max;
}

void bhv_exists_when_init(Actor *a, const void *params) {
    if (!in_range(params)) actor_destroy(a);
}

void bhv_exists_when_update(Actor *a, const void *params) {
    const Params_exists_when *p = params;
    if (p->live && !in_range(p)) actor_destroy(a);
}
