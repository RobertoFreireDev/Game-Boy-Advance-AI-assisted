// leaper - side view. When the player is near it walks toward them; every so often it
// crouches (plays 'attack' as a warning), then leaps in an arc that lands near where the
// player stood, and runs its on_land actions (a camera shake, dust). Bosses and hoppers.
#include "leaper.h"
#include "behavior_params.h"
#include "engine/actions.h"

enum { ST_PHASE, ST_TIMER };
enum { PH_GROUND, PH_WINDUP, PH_AIR };

#define MIN_AIR_TICKS 4     // ignore "on the ground" for this long after taking off

void bhv_leaper_update(Actor *a, const void *params) {
    const Params_leaper *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_PHASE] == PH_AIR) {
        if (st[ST_TIMER]) st[ST_TIMER]--;
        else if (a->on_ground) {                    // landed
            st[ST_PHASE] = PH_GROUND;
            st[ST_TIMER] = (u32)p->every;
            a->vx = 0;
            actor_sound(a, SND_LAND);
            scripts_start(p->on_land, a);
        }
        if (!a->anim_lock && st[ST_PHASE] == PH_AIR) actor_play_slot(a, a->vy < 0 ? ANIM_JUMP : ANIM_FALL);
        return;
    }
    if (a->anim_lock) return;                       // knocked back by a hit
    Actor *pl = actor_player();
    s32 dx = pl ? fx_to_int(pl->x - a->x) : 0, dy = pl ? fx_to_int(pl->y - a->y) : 0;
    if (!pl || a->stun || fx_abs(dx) > p->range || fx_abs(dy) > p->range) {
        a->vx = 0;
        st[ST_PHASE] = PH_GROUND;
        actor_play_slot(a, ANIM_IDLE);
        return;
    }
    if (st[ST_PHASE] == PH_WINDUP) {
        a->vx = 0;
        if (st[ST_TIMER] && --st[ST_TIMER]) return;
        // Leap: flight time = 2 * speed / gravity; cover the distance to the player in it.
        s32 air = fx_to_int(fx_div(2 * p->leap_speed, g_game.gravity));
        s32 reach = dx > p->max_reach ? p->max_reach : dx < -p->max_reach ? -p->max_reach : dx;
        a->vy = -p->leap_speed;
        a->vx = air > 0 ? reach * FX_ONE / air : 0;
        a->facing_left = dx < 0;
        st[ST_PHASE] = PH_AIR;
        st[ST_TIMER] = MIN_AIR_TICKS;
        actor_sound(a, SND_JUMP);
        actor_play_slot(a, ANIM_JUMP);
        return;
    }
    a->facing_left = dx < 0;
    a->vx = fx_abs(dx) > 4 ? fx_sign(dx) * p->walk_speed : 0;
    actor_play_slot(a, a->vx ? ANIM_WALK : ANIM_IDLE);
    if (st[ST_TIMER]) {
        st[ST_TIMER]--;
        return;
    }
    st[ST_PHASE] = PH_WINDUP;
    st[ST_TIMER] = (u32)p->windup;
    a->vx = 0;
    actor_play_slot(a, ANIM_ATTACK);
    anim_start(&a->anim, a->anim.anim, a);
}
