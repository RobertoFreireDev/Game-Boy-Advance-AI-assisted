// fish_bite - a fish shadow in the water that the player catches with the rod.
//   Press the button facing the shadow (close enough, with the rod variable on): the line is
//   cast and the player is held still. After a wait the fish nibbles 1..nibbles_max times
//   (pressing now scares it off: on_spook), then bites: pressing within `window` ticks
//   catches it (on_catch, the shadow plays 'die'), too late and it gets away (on_escape).
//   The cancel button reels the line back in. Body slots: walk/idle = swimming, jump = bobber
//   floating, attack = bobber dipping (nibble), hurt = bobber pulled under (bite), die = catch.
//   Sounds: jump = cast, attack = nibble, hurt = bite.
// Only one fish can be on the line at a time.
#include "fish_bite.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/ui.h"
#include "engine/vars.h"
#include "engine/core.h"
#include "engine/actions.h"

enum { ST_STATE, ST_TIMER, ST_NIBBLES };
enum { S_SWIM, S_WAIT, S_NIBBLE, S_BITE };

#define NIBBLE_TICKS 12

static u32 s_busy_frame;            // last frame a fish was on the line (+1)

static int facing_toward(const Actor *pl, s32 dx, s32 dy) {
    switch (pl->facing) {
    case DIR_LEFT:  return dx < 0 && fx_abs(dy) <= fx_abs(dx) + 8;
    case DIR_RIGHT: return dx > 0 && fx_abs(dy) <= fx_abs(dx) + 8;
    case DIR_UP:    return dy < 0 && fx_abs(dx) <= fx_abs(dy) + 8;
    default:        return dy > 0 && fx_abs(dx) <= fx_abs(dy) + 8;
    }
}

static void finish(Actor *a, u32 *st) {
    st[ST_STATE] = S_SWIM;
    a->anim_lock = 0;
    actor_play_slot(a, ANIM_IDLE);
}

void bhv_fish_bite_update(Actor *a, const void *params) {
    const Params_fish_bite *p = params;
    u32 *st = bhv_state(a);
    Actor *pl = actor_player();
    if (!pl) return;
    if (st[ST_STATE] == S_SWIM) {
        if (!input_pressed(p->button) || ui_blocking() || pl->anim_lock || pl->stun) return;
        if (p->var >= 0 && vars_get(p->var) == 0) return;
        if (s_busy_frame && s_busy_frame + 2 >= core_frame()) return;     // another fish is hooked
        s32 al, at, ar, ab, l, t, r, b;
        actor_rect(a, &al, &at, &ar, &ab);
        actor_rect(pl, &l, &t, &r, &b);
        if (al > r + p->range || ar < l - p->range || at > b + p->range || ab < t - p->range) return;
        if (!facing_toward(pl, (al + ar - l - r) / 2, (at + ab - t - b) / 2)) return;
        st[ST_STATE] = S_WAIT;
        st[ST_TIMER] = (u32)rng_range(p->wait_min, p->wait_max);
        st[ST_NIBBLES] = (u32)rng_range(1, p->nibbles_max);
        actor_sound(a, SND_JUMP);
        actor_play_slot(a, ANIM_JUMP);
        s_busy_frame = core_frame();
        a->vx = a->vy = 0;
        a->anim_lock = 2;
        pl->vx = pl->vy = 0;
        pl->stun = 2;
        actor_play_slot(pl, ANIM_IDLE);
        return;                                 // the press that cast doesn't also pull
    }
    // A fish is on the line: hold the fish and the player still.
    s_busy_frame = core_frame();
    a->vx = a->vy = 0;
    a->anim_lock = 2;
    pl->vx = pl->vy = 0;
    pl->stun = 2;
    if (input_pressed(p->cancel_button)) {                      // reel in
        finish(a, st);
        return;
    }
    int press = input_pressed(p->button);
    if (st[ST_STATE] == S_BITE) {
        if (press) {
            st[ST_STATE] = S_SWIM;
            actor_die(a, 1);
            scripts_start(p->on_catch, a);
            return;
        }
        if (st[ST_TIMER] == 0 || --st[ST_TIMER] == 0) {         // too late
            scripts_start(p->on_escape, a);
            actor_destroy(a);
        }
        return;
    }
    if (press) {                                                // too early: scared away
        scripts_start(p->on_spook, a);
        actor_destroy(a);
        return;
    }
    if (st[ST_TIMER] && --st[ST_TIMER]) return;
    if (st[ST_STATE] == S_NIBBLE || st[ST_NIBBLES] == 0) {     // a dip ends (or time to bite)
        if (st[ST_NIBBLES] == 0) {
            st[ST_STATE] = S_BITE;
            st[ST_TIMER] = (u32)p->window;
            actor_sound(a, SND_HURT);
            actor_play_slot(a, ANIM_HURT);
        } else {
            st[ST_STATE] = S_WAIT;
            st[ST_TIMER] = (u32)rng_range(36, 84);
            actor_play_slot(a, ANIM_JUMP);
        }
        return;
    }
    st[ST_STATE] = S_NIBBLE;                                    // the bobber dips
    st[ST_TIMER] = NIBBLE_TICKS;
    st[ST_NIBBLES]--;
    actor_sound(a, SND_ATTACK);
    actor_play_slot(a, ANIM_ATTACK);
}
