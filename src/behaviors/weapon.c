// weapon - auto-firing weapon: every few ticks it launches projectile objects on its own (no
// button). Its level is a game variable (0 = not owned yet). Each level above 1 fires faster
// and hits harder; every few levels one more projectile joins the volley.
#include "weapon.h"
#include "behavior_params.h"
#include "projectile.h"
#include "engine/vars.h"
#include "engine/scene.h"
#include "engine/audio.h"

enum { ST_TIMER };

#define HASTE_PERCENT 8     // each point of haste_var shortens the wait by 8%
#define RETRY_TICKS   8     // nothing to aim at: look again this soon

// Unit vector (fixed) from a pixel offset.
static void unit(s32 dx, s32 dy, fixed *ux, fixed *uy) {
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
    if (len == 0) { *ux = FX_ONE; *uy = 0; return; }
    *ux = dx * FX_ONE / len;
    *uy = dy * FX_ONE / len;
}

// Closest live enemy within range of (x, y), or NULL.
static Actor *nearest_enemy(s32 x, s32 y, s32 range) {
    Actor *best = NULL;
    s32 best_d = range * range;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (o->category != CAT_ENEMY || !actor_alive(o)) continue;
        s32 dx = fx_to_int(o->x) - x, dy = fx_to_int(o->y) + o->hb_y + o->hb_h / 2 - y;
        if (fx_abs(dx) > range || fx_abs(dy) > range) continue;
        s32 d = dx * dx + dy * dy;
        if (d <= best_d) { best_d = d; best = o; }
    }
    return best;
}

// Which way the owner is going (or facing when standing still), as a unit vector.
static void facing_dir(const Actor *a, fixed *ux, fixed *uy) {
    s32 dx = fx_sign(a->vx), dy = fx_sign(a->vy);
    if (!dx && !dy) {
        dx = a->facing == DIR_RIGHT ? 1 : a->facing == DIR_LEFT ? -1 : 0;
        dy = a->facing == DIR_DOWN ? 1 : a->facing == DIR_UP ? -1 : 0;
    }
    unit(dx, dy, ux, uy);
}

void bhv_weapon_update(Actor *a, const void *params) {
    const Params_weapon *p = params;
    u32 *st = bhv_state(a);
    s32 lv = vars_get(p->level_var);
    if (lv <= 0) return;
    if (st[ST_TIMER]) { st[ST_TIMER]--; return; }

    s32 cx = fx_to_int(a->x), cy = fx_to_int(a->y) + a->hb_y + a->hb_h / 2;
    fixed ux = FX_ONE, uy = 0;
    switch (p->aim) {
    case WEAPON_AIM_NEAREST: {
        Actor *t = nearest_enemy(cx, cy, p->range);
        if (!t) { st[ST_TIMER] = RETRY_TICKS; return; }
        unit(fx_to_int(t->x) - cx, fx_to_int(t->y) + t->hb_y + t->hb_h / 2 - cy, &ux, &uy);
        break;
    }
    case WEAPON_AIM_FACING:
        facing_dir(a, &ux, &uy);
        break;
    }

    int count = p->count + (p->extra_every > 0 ? (lv - 1) / p->extra_every : 0);
    s32 base = rng_range(0, 359);
    // Even spacing: step i is at i * 360 / count degrees, kept as a running quotient and
    // remainder so the volley needs one division, not one per shot (no fast divide on the GBA).
    if (count < 1) count = 1;
    s32 step_q = 360 / count, step_r = 360 % count, q = 0, r = 0;
    for (int i = 0; i < count; i++) {
        fixed dx = ux, dy = uy;
        s32 angle = 0;
        switch (p->aim) {
        case WEAPON_AIM_NEAREST:
        case WEAPON_AIM_FACING: {           // fan the volley out around the aim
            s32 off = (2 * i - (count - 1)) * p->spread / 2;
            fixed c = fx_cos_deg(off), s = fx_sin_deg(off);
            dx = fx_mul(ux, c) - fx_mul(uy, s);
            dy = fx_mul(ux, s) + fx_mul(uy, c);
            break;
        }
        case WEAPON_AIM_RANDOM:
            angle = rng_range(0, 359);
            dx = fx_cos_deg(angle);
            dy = -fx_sin_deg(angle);
            break;
        case WEAPON_AIM_AROUND:
        case WEAPON_AIM_ORBIT:              // evenly spaced around the owner
            angle = base + q;
            dx = fx_cos_deg(angle);
            dy = -fx_sin_deg(angle);
            break;
        }
        q += step_q;
        r += step_r;
        if (r >= count) { r -= count; q++; }
        Actor *shot = scene_spawn(p->projectile, cx, cy);
        if (!shot) break;                   // actor pool full
        projectile_launch(shot, a, dx, dy, angle, p->damage_per_level * (lv - 1));
    }
    if (p->sound) audio_play_sfx(p->sound);

    s32 cd = p->cooldown - p->cooldown_per_level * (lv - 1);
    if (p->haste_var >= 0) {
        s32 pct = 100 - HASTE_PERCENT * vars_get(p->haste_var);
        cd = cd * (pct < 40 ? 40 : pct) / 100;
    }
    st[ST_TIMER] = (u32)(cd < p->min_cooldown ? p->min_cooldown : cd);
}
