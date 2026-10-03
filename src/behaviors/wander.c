// wander - walk in a random direction (up, down, left or right) for a while, rest, pick a new
// direction. Turns when it bumps into something. Can be kept near its starting point.
#include "wander.h"
#include "behavior_params.h"

enum { ST_DIR, ST_TIMER, ST_MOVED };

#define DIR_REST 4

static const s8 s_dx[5] = { 1, -1, 0, 0, 0 };
static const s8 s_dy[5] = { 0, 0, -1, 1, 0 };

static void pick(Actor *a, const Params_wander *p, u32 *st, int resting_allowed) {
    u32 dir = (u32)rng_range(0, 3);
    if (resting_allowed && p->pause_ticks > 0 && st[ST_DIR] != DIR_REST) {
        st[ST_DIR] = DIR_REST;
        st[ST_TIMER] = (u32)p->pause_ticks;
        return;
    }
    if (dir == st[ST_DIR]) dir = (dir + (u32)rng_range(1, 3)) & 3;    // a new direction
    if (p->distance > 0) {                                           // too far: head home
        s32 ox = fx_to_int(a->x) - a->home_x, oy = fx_to_int(a->y) - a->home_y;
        if (fx_abs(ox) > p->distance || fx_abs(oy) > p->distance)
            dir = fx_abs(ox) >= fx_abs(oy) ? (ox > 0 ? DIR_LEFT : DIR_RIGHT) : (oy > 0 ? DIR_UP : DIR_DOWN);
    }
    st[ST_DIR] = dir;
    st[ST_TIMER] = (u32)rng_range(p->min_ticks, p->max_ticks);
    st[ST_MOVED] = 0;
}

void bhv_wander_init(Actor *a, const void *params) {
    u32 *st = bhv_state(a);
    st[ST_DIR] = DIR_REST;
    pick(a, params, st, 0);
    a->gravity = 0;
}

void bhv_wander_update(Actor *a, const void *params) {
    const Params_wander *p = params;
    u32 *st = bhv_state(a);
    if (a->anim_lock) return;                       // being knocked back
    int blocked = st[ST_DIR] != DIR_REST && st[ST_MOVED] && a->x == a->prev_x && a->y == a->prev_y;
    if (st[ST_TIMER] == 0 || blocked || a->hit_wall) pick(a, p, st, !blocked && !a->hit_wall);
    else st[ST_TIMER]--;
    st[ST_MOVED] = 1;
    int dx = s_dx[st[ST_DIR]], dy = s_dy[st[ST_DIR]];
    a->vx = dx * p->speed;
    a->vy = dy * p->speed;
    actor_face(a, dx, dy);
    actor_play_slot(a, (dx || dy) ? ANIM_WALK : ANIM_IDLE);
}
