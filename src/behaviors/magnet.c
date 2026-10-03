// magnet - a pickup that flies to the player once the player comes within range, speeding up
// until it is caught. The range can grow with a game variable (pickup-range upgrades).
// With pull_after, a pickup left lying around flies to the player on its own after a while,
// so nothing is lost and the actor pool does not fill up with old gems.
#include "magnet.h"
#include "behavior_params.h"
#include "engine/vars.h"

enum { ST_PULLED, ST_SPEED, ST_AGE };

#define RANGE_PER_POINT 12          // pixels of range added per point of range_var

void bhv_magnet_init(Actor *a, const void *params) {
    (void)params;
    a->gravity = 0;
}

void bhv_magnet_update(Actor *a, const void *params) {
    const Params_magnet *p = params;
    u32 *st = bhv_state(a);
    Actor *pl = actor_player();
    if (!pl) { a->vx = a->vy = 0; return; }
    s32 dx = fx_to_int(pl->x - a->x);
    s32 dy = fx_to_int(pl->y - a->y) + pl->hb_y + pl->hb_h / 2;
    if (!st[ST_PULLED]) {
        s32 r = p->range + (p->range_var >= 0 ? RANGE_PER_POINT * vars_get(p->range_var) : 0);
        int old = p->pull_after > 0 && ++st[ST_AGE] >= (u32)p->pull_after;
        if (!old && (fx_abs(dx) > r || fx_abs(dy) > r || dx * dx + dy * dy > r * r)) return;
        st[ST_PULLED] = 1;
        st[ST_SPEED] = (u32)p->acceleration;
    }
    fixed sp = (fixed)st[ST_SPEED] + p->acceleration;
    if (sp > p->max_speed) sp = p->max_speed;
    st[ST_SPEED] = (u32)sp;
    // Distance estimate (largest side + 3/8 of the other, within 7%) and one division: a screen
    // full of flying gems runs this every tick, and a square root plus two divisions per gem
    // was one of the biggest costs of a big horde.
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    s32 len = ax > ay ? ax + (ay * 3 >> 3) : ay + (ax * 3 >> 3);
    if (len == 0) return;
    s32 k = (sp << 8) / len;
    a->vx = (dx * k) >> 8;
    a->vy = (dy * k) >> 8;
}
