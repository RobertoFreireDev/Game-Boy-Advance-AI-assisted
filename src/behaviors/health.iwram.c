// health.iwram.c (hot code: ARM in IWRAM) - hit points, invincibility after a hit, knockback,
// death. Spikes and falling off the map also hurt. Other behaviors call health_damage().
// In IWRAM because every monster of a horde runs it each tick and every shot hit calls it.
// Side view (body with gravity): knocked back sideways and up, dies by falling off the screen.
// Top-down (no gravity): knocked straight away from the hit, slowing down; dies in place.
#include "health.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"

enum { ST_HP, ST_INVINCIBLE };

#define TOPDOWN_KNOCK_TICKS 12

static int topdown(const Actor *a) { return !a->body || !a->body->gravity; }

static s32 hp_get(const Params_health *p, const u32 *st) {
    return p->var >= 0 ? vars_get(p->var) : (s32)st[ST_HP];
}

static void hp_set(const Params_health *p, u32 *st, s32 v) {
    if (v < 0) v = 0;
    if (p->var >= 0) vars_set(p->var, v);
    else st[ST_HP] = (u32)v;
}

static void kill(Actor *a, const Params_health *p, u32 *st) {
    hp_set(p, st, 0);
    a->climbing = 0;
    actor_sound(a, SND_DIE);
    scripts_start(p->on_death, a);
    actor_die(a, topdown(a));
}

void bhv_health_init(Actor *a, const void *params) {
    const Params_health *p = params;
    u32 *st = bhv_state(a);
    st[ST_HP] = (u32)p->max;
    st[ST_INVINCIBLE] = 0;
}

void bhv_health_update(Actor *a, const void *params) {
    const Params_health *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_INVINCIBLE]) st[ST_INVINCIBLE]--;
    if (topdown(a) && a->anim_lock) {           // top-down knockback slows down
        a->vx = a->vx * 3 / 4;
        a->vy = a->vy * 3 / 4;
    }
    if (a->flags & ACTOR_FELL) {
        a->flags &= (u8)~ACTOR_FELL;
        kill(a, p, st);
        return;
    }
    if (p->spikes && (a->tile_touch & TILE_HAZARD)) health_damage(a, 1, NULL);
}

int health_damage(Actor *a, int amount, const Actor *source) {
    int idx = actor_logic_index(a, BHV_HEALTH);
    if (idx < 0 || !actor_alive(a)) return 0;
    const Params_health *p = a->logic[idx].params;
    u32 *st = actor_state_of(a, idx);
    if (st[ST_INVINCIBLE]) return 0;
    s32 hp = hp_get(p, st) - amount;
    hp_set(p, st, hp);
    st[ST_INVINCIBLE] = (u32)p->invincible_ticks;
    a->blink = (u16)p->invincible_ticks;
    if (!p->stagger) {
        // Bosses only blink: no knockback, no hurt pose, whatever they were doing goes on.
    } else if (topdown(a)) {
        // Knockback: straight away from what hit us (or backward for spikes), 4 directions.
        s32 dx = source ? fx_to_int(a->x - source->x) : 0;
        s32 dy = source ? fx_to_int(a->y - source->y) : 0;
        if (!source) {
            dx = a->facing == DIR_RIGHT ? -1 : a->facing == DIR_LEFT ? 1 : 0;
            dy = a->facing == DIR_DOWN ? -1 : a->facing == DIR_UP ? 1 : 0;
        }
        if (fx_abs(dx) >= fx_abs(dy)) dy = 0;
        else dx = 0;
        a->vx = fx_sign(dx) * p->knockback * 2;
        a->vy = fx_sign(dy) * p->knockback * 2;
        a->anim_lock = TOPDOWN_KNOCK_TICKS;
    } else {
        // Knockback: away from what hit us (or backward for spikes).
        int dir = source ? (source->x < a->x ? 1 : -1) : (a->facing_left ? 1 : -1);
        a->vx = dir * p->knockback;
        if (a->climbing) {
            a->climbing = 0;
            a->gravity = a->body ? a->body->gravity : 1;
        }
        if (a->gravity) a->vy = -fx_from_int(2);
        a->anim_lock = 20;
    }
    if (p->stagger) actor_play_slot(a, ANIM_HURT);
    if (hp <= 0) {
        kill(a, p, st);
    } else {
        actor_sound(a, SND_HURT);
        scripts_start(p->on_hurt, a);
    }
    return 1;
}
