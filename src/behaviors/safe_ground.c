// safe_ground - remembers (in two game variables) the last spot where the actor stood firmly on
// the ground, away from spikes. Touching spikes or falling off the map costs some health and
// puts it back on that spot. List it before health so it handles spikes and pits first.
#include "safe_ground.h"
#include "behavior_params.h"
#include "health.h"
#include "engine/vars.h"
#include "engine/physics.h"

enum { ST_KNOWN };          // 1 once a safe spot was recorded in this scene

void bhv_safe_ground_update(Actor *a, const void *params) {
    const Params_safe_ground *p = params;
    u32 *st = bhv_state(a);
    int fell = (a->flags & ACTOR_FELL) != 0;
    if (!st[ST_KNOWN] && (a->tile_touch & TILE_HAZARD)) {
        health_damage(a, p->damage, NULL);          // no safe spot yet: spikes just hurt
        if (!actor_alive(a)) return;
    } else if (st[ST_KNOWN] && (fell || (a->tile_touch & TILE_HAZARD))) {
        a->flags &= (u8)~ACTOR_FELL;
        health_damage(a, p->damage, NULL);          // no-op while still blinking from a hit
        if (!actor_alive(a)) return;
        a->x = a->prev_x = fx_from_int(vars_get(p->x_var));
        a->y = a->prev_y = fx_from_int(vars_get(p->y_var));
        a->vx = a->vy = 0;
        a->tile_touch = 0;
        return;
    }
    if (!a->on_ground || a->tile_touch || a->riding >= 0) return;
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    // Both feet on ground and no spikes just beside: a fair place to come back to.
    if (!physics_ground_at(l, b + 1) || !physics_ground_at(r, b + 1)) return;
    if ((physics_tile_at(l - 8, b) | physics_tile_at(r + 8, b)) & TILE_HAZARD) return;
    vars_set(p->x_var, fx_to_int(a->x));
    vars_set(p->y_var, fx_to_int(a->y));
    st[ST_KNOWN] = 1;
}
