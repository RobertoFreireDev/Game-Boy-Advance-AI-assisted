// aura - a damaging field around its owner: every few ticks it hurts (and pushes away) every
// enemy within a radius. Its level is a game variable (0 = not owned yet); each level above 1
// widens it and hits harder.
#include "aura.h"
#include "behavior_params.h"
#include "health.h"
#include "engine/vars.h"
#include "engine/particles.h"

enum { ST_TIMER };

#define HASTE_PERCENT 8

void bhv_aura_update(Actor *a, const void *params) {
    const Params_aura *p = params;
    u32 *st = bhv_state(a);
    s32 lv = vars_get(p->level_var);
    if (lv <= 0) return;
    if (st[ST_TIMER]) { st[ST_TIMER]--; return; }

    s32 power = p->power_var >= 0 ? vars_get(p->power_var) : 0;
    s32 dmg = (p->damage + p->damage_per_level * (lv - 1)) * (10 + power) / 10;
    s32 r = p->radius + p->radius_per_level * (lv - 1);
    s32 cx = fx_to_int(a->x), cy = fx_to_int(a->y) + a->hb_y + a->hb_h / 2;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (o->category != CAT_ENEMY || !actor_alive(o)) continue;
        s32 dx = fx_to_int(o->x) - cx, dy = fx_to_int(o->y) + o->hb_y + o->hb_h / 2 - cy;
        if (fx_abs(dx) > r || fx_abs(dy) > r || dx * dx + dy * dy > r * r) continue;
        health_damage(o, dmg < 1 ? 1 : dmg, a);
    }
    if (p->particle) particles_burst(p->particle, cx, cy);

    s32 every = p->every;
    if (p->haste_var >= 0) {
        s32 pct = 100 - HASTE_PERCENT * vars_get(p->haste_var);
        every = every * (pct < 40 ? 40 : pct) / 100;
    }
    st[ST_TIMER] = (u32)(every < 4 ? 4 : every);
}
