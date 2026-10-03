// soul_magic - spends a soul variable. Holding the button (standing, hurt, enough soul)
// focuses: the actor stands still and after a while heals 1 health for one soul cost.
// A quick tap casts the spell instead (once it is unlocked): an object (with the projectile
// behavior) launched the way the actor faces. Getting hit cancels a focus.
#include "soul_magic.h"
#include "behavior_params.h"
#include "projectile.h"
#include "engine/input.h"
#include "engine/vars.h"
#include "engine/scene.h"
#include "engine/particles.h"

enum { ST_ACTIVE, ST_HELD, ST_FOCUS };

static s32 max_health(const Params_soul_magic *p) {
    return p->max_health_var >= 0 ? vars_get(p->max_health_var) : p->max_health;
}

static void cast(Actor *a, const Params_soul_magic *p) {
    if (p->spell < 0 || (p->spell_var >= 0 && vars_get(p->spell_var) == 0)) return;
    if (vars_get(p->soul_var) < p->cost) return;
    int dir = a->facing_left ? -1 : 1;
    Actor *shot = scene_spawn(p->spell, fx_to_int(a->x) + dir * 8, fx_to_int(a->y) + p->spell_y);
    if (!shot) return;
    vars_add(p->soul_var, -p->cost);
    projectile_launch(shot, a, dir * FX_ONE, 0, 0, 0);
    shot->facing_left = dir < 0;
    a->vx = -dir * p->recoil;
    if (a->body && a->body->anims[ANIM_CAST]) {
        a->anim_slot = ANIM_CAST;
        anim_start(&a->anim, a->body->anims[ANIM_CAST], a);
    }
    actor_sound(a, SND_CAST);
}

void bhv_soul_magic_update(Actor *a, const void *params) {
    const Params_soul_magic *p = params;
    u32 *st = bhv_state(a);
    if (input_pressed(p->button) && !a->anim_lock) {
        st[ST_ACTIVE] = 1;
        st[ST_HELD] = st[ST_FOCUS] = 0;
    }
    if (!st[ST_ACTIVE]) return;
    if (a->anim_slot == ANIM_HURT && (st[ST_FOCUS] || a->anim_lock > 2)) {   // a hit breaks it
        st[ST_ACTIVE] = st[ST_FOCUS] = 0;
        return;
    }
    if (input_held(p->button)) {
        if (++st[ST_HELD] <= (u32)p->hold_ticks) return;
        s32 hp = vars_get(p->health_var);
        if (vars_get(p->soul_var) < p->cost || hp >= max_health(p) || !a->on_ground) {
            st[ST_FOCUS] = 0;                       // nothing to heal (or no soul): just wait
            return;
        }
        st[ST_FOCUS]++;
        a->vx = 0;
        a->anim_lock = 2;                           // the controller waits while focusing
        actor_play_slot(a, ANIM_FOCUS);
        if (p->particle && (st[ST_FOCUS] & 7) == 1)
            particles_burst(p->particle, fx_to_int(a->x), fx_to_int(a->y) + a->hb_y + a->hb_h / 2);
        if (st[ST_FOCUS] >= (u32)p->focus_ticks) {
            vars_add(p->soul_var, -p->cost);
            vars_set(p->health_var, hp + 1);
            actor_sound(a, SND_HEAL);
            st[ST_FOCUS] = 0;
        }
        return;
    }
    if (st[ST_HELD] <= (u32)p->hold_ticks) cast(a, p);     // released quickly: a tap
    st[ST_ACTIVE] = st[ST_FOCUS] = 0;
}
