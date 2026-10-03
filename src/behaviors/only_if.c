// only_if - checks a game variable every tick. mode remove: the object is removed as soon as
// the check is false (a boss already beaten, a grub already freed). mode hide: while false it
// is invisible and nothing can touch it; it comes back when the check turns true (a reward
// that appears once the boss falls, a gate that opens).
#include "only_if.h"
#include "behavior_params.h"
#include "engine/vars.h"

enum { ST_HIDDEN };

static int check(const Params_only_if *p) {
    s32 v = vars_get(p->var);
    s32 w = p->value_var >= 0 ? vars_get(p->value_var) : p->value;
    switch (p->op) {
    case CMP_EQ: return v == w;
    case CMP_NE: return v != w;
    case CMP_LT: return v < w;
    case CMP_LE: return v <= w;
    case CMP_GT: return v > w;
    case CMP_GE: return v >= w;
    }
    return 0;
}

void bhv_only_if_update(Actor *a, const void *params) {
    const Params_only_if *p = params;
    u32 *st = bhv_state(a);
    int ok = check(p);
    if (p->mode == ONLY_IF_MODE_REMOVE) {
        if (!ok) actor_destroy(a);
        return;
    }
    if (!ok && !st[ST_HIDDEN]) {
        st[ST_HIDDEN] = 1;
        a->flags |= ACTOR_HIDDEN;
        a->category = CAT_COUNT;                    // out of every contact bucket
        a->collides &= 1u << CAT_TILES;
        a->solid_mode = SOLID_NONE;
    } else if (ok && st[ST_HIDDEN]) {
        st[ST_HIDDEN] = 0;
        if (a->body) a->flags &= (u8)~ACTOR_HIDDEN;
        a->category = a->data->category;
        a->collides = a->body ? a->body->collides : a->collides;
        a->solid_mode = a->body && a->body->solid ? SOLID_FULL : SOLID_NONE;
    }
}
