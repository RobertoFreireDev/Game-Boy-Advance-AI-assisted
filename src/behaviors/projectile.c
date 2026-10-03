// projectile - a launched shot: flies straight (or circles its owner), hurts the enemies it
// touches and vanishes after its lifetime or after hitting enough enemies. Weapons launch it
// with projectile_launch(); placed on its own it just flies right.
#include "projectile.h"
#include "behavior_params.h"
#include "health.h"
#include "engine/vars.h"

enum { ST_LIFE, ST_DAMAGE, ST_HITS, ST_ANGLE, ST_OWNER };

// Damage with the power bonus (+10% per point of power_var).
static u32 total_damage(const Params_projectile *p, s32 bonus) {
    s32 power = p->power_var >= 0 ? vars_get(p->power_var) : 0;
    s32 d = (p->damage + bonus) * (10 + power) / 10;
    return (u32)(d < 1 ? 1 : d);
}

void bhv_projectile_init(Actor *a, const void *params) {
    const Params_projectile *p = params;
    u32 *st = bhv_state(a);
    st[ST_LIFE] = (u32)p->lifetime;
    st[ST_DAMAGE] = total_damage(p, 0);
    st[ST_HITS] = (u32)p->pierce;
    st[ST_ANGLE] = 0;
    st[ST_OWNER] = 0;
    a->gravity = 0;
    a->vx = p->motion == PROJECTILE_MOTION_STRAIGHT ? p->speed : 0;
}

void projectile_launch(Actor *shot, const Actor *owner, fixed dx, fixed dy, s32 angle, s32 bonus_damage) {
    int idx = actor_logic_index(shot, BHV_PROJECTILE);
    if (idx < 0) return;
    const Params_projectile *p = shot->logic[idx].params;
    u32 *st = actor_state_of(shot, idx);
    st[ST_DAMAGE] = total_damage(p, bonus_damage);
    st[ST_ANGLE] = (u32)(((angle % 360) + 360) % 360);
    st[ST_OWNER] = owner ? (u32)(actor_index(owner) + 1) : 0;
    if (p->motion == PROJECTILE_MOTION_STRAIGHT) {
        shot->vx = fx_mul(dx, p->speed);
        shot->vy = fx_mul(dy, p->speed);
        if (shot->vx) shot->facing_left = shot->vx < 0;
    }
}

void bhv_projectile_update(Actor *a, const void *params) {
    const Params_projectile *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_LIFE] && --st[ST_LIFE] == 0) {
        actor_destroy(a);
        return;
    }
    if (p->motion != PROJECTILE_MOTION_ORBIT) return;
    // Circle the owner's middle; the speed is set so physics lands exactly on the circle.
    Actor *o = st[ST_OWNER] ? &g_actors[st[ST_OWNER] - 1] : NULL;
    if (!o || !actor_alive(o)) {
        actor_destroy(a);
        return;
    }
    st[ST_ANGLE] = (st[ST_ANGLE] + (u32)p->spin) % 360;
    fixed cx = o->x, cy = o->y + fx_from_int(o->hb_y + o->hb_h / 2);
    fixed tx = cx + fx_cos_deg((s32)st[ST_ANGLE]) * p->radius;
    fixed ty = cy - fx_sin_deg((s32)st[ST_ANGLE]) * p->radius;
    a->vx = tx - a->x;
    a->vy = ty - a->y;
}

void bhv_projectile_on_touch(Actor *self, Actor *other, const void *params) {
    const Params_projectile *p = params;
    u32 *st = bhv_state(self);
    if (other->category != CAT_ENEMY) return;
    if (!health_damage(other, (int)st[ST_DAMAGE], self)) return;     // still blinking from a hit
    if (p->pierce > 0 && --st[ST_HITS] == 0) actor_destroy(self);
}
