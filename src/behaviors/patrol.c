// patrol - walk back and forth, turning at walls, ledge edges or a distance limit.
#include "patrol.h"
#include "behavior_params.h"
#include "engine/physics.h"

void bhv_patrol_init(Actor *a, const void *params) {
    const Params_patrol *p = params;
    bhv_state(a)[0] = (u32)(p->start_left ? -1 : 1);
}

void bhv_patrol_update(Actor *a, const void *params) {
    const Params_patrol *p = params;
    u32 *st = bhv_state(a);
    s32 dir = (s32)st[0];
    if (a->hit_wall) {
        dir = -dir;
    } else if (p->turn_at_edges && a->on_ground) {
        s32 l, t, r, b;
        actor_rect(a, &l, &t, &r, &b);
        if (!physics_ground_at(dir > 0 ? r + 1 : l - 1, b + 1)) dir = -dir;
    }
    if (p->distance > 0) {
        s32 off = fx_to_int(a->x) - a->home_x;
        if (off >= p->distance) dir = -1;
        else if (off <= -p->distance) dir = 1;
    }
    st[0] = (u32)dir;
    if (a->anim_lock) return;
    a->vx = dir * p->speed;
    a->facing_left = dir < 0;
    actor_play_slot(a, ANIM_WALK);
}
