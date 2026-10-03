// platformer_controller - side-view running, jumping (variable height) and ladder climbing.
#include "platformer_controller.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/physics.h"

enum { ST_COYOTE, ST_JUMP_V, ST_BUFFER, ST_JUMPING };

#define COYOTE_TICKS 6      // can still jump this long after walking off a ledge
#define BUFFER_TICKS 6      // a jump pressed this long before landing still counts

void bhv_platformer_controller_init(Actor *a, const void *params) {
    const Params_platformer_controller *p = params;
    u32 *st = bhv_state(a);
    // Launch speed for the requested height: v = sqrt(2 * gravity * height).
    st[ST_JUMP_V] = fx_isqrt((u32)(2 * g_game.gravity * p->jump_height * FX_ONE));
}

static void stop_climbing(Actor *a) {
    a->climbing = 0;
    a->gravity = a->body ? a->body->gravity : 1;
}

// A ladder tile right under the feet (standing on top of a ladder).
static int ladder_below(const Actor *a) {
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    return (physics_tile_at((l + r) / 2, b + 1) & TILE_LADDER) != 0;
}

void bhv_platformer_controller_update(Actor *a, const void *params) {
    const Params_platformer_controller *p = params;
    u32 *st = bhv_state(a);
    int dx = input_dir_x(), dy = input_dir_y();
    fixed jump_v = (fixed)st[ST_JUMP_V];
    int on_ladder = (a->tile_touch & TILE_LADDER) != 0;

    // Ladders.
    if (a->climbing) {
        if (!on_ladder && (dy < 0 || !ladder_below(a))) {
            stop_climbing(a);                           // climbed off the top (or the ladder ended)
            if (dy < 0) a->vy = -jump_v / 3;
        } else if (a->on_ground && dy > 0 && !ladder_below(a)) {
            stop_climbing(a);                           // reached the bottom
        } else if (input_pressed(p->jump_button)) {
            stop_climbing(a);
            a->vy = -jump_v * 3 / 4;
            actor_sound(a, SND_JUMP);
        } else {
            a->vx = dx * p->climb_speed / 2;
            a->vy = dy * p->climb_speed;
            if (dx) a->facing_left = dx < 0;
            if (!a->anim_lock) actor_play_slot(a, ANIM_CLIMB);
            return;
        }
    } else if ((on_ladder && dy < 0) || (dy > 0 && (ladder_below(a) || (on_ladder && !a->on_ground)))) {
        a->climbing = 1;
        a->gravity = 0;
        a->vx = a->vy = 0;
        return;
    }

    // Running (knockback keeps control away for a moment).
    if (!a->anim_lock) {
        fixed target = dx * p->speed;
        if (a->vx < target) {
            a->vx += p->acceleration;
            if (a->vx > target) a->vx = target;
        } else if (a->vx > target) {
            a->vx -= p->acceleration;
            if (a->vx < target) a->vx = target;
        }
        if (dx) a->facing_left = dx < 0;
    }

    // Jumping.
    if (a->on_ground) st[ST_COYOTE] = COYOTE_TICKS;
    else if (st[ST_COYOTE]) st[ST_COYOTE]--;
    if (input_pressed(p->jump_button)) st[ST_BUFFER] = BUFFER_TICKS;
    else if (st[ST_BUFFER]) st[ST_BUFFER]--;
    if (st[ST_BUFFER] && st[ST_COYOTE]) {
        a->vy = -jump_v;
        st[ST_COYOTE] = st[ST_BUFFER] = 0;
        st[ST_JUMPING] = 1;
        actor_sound(a, SND_JUMP);
    }
    if (st[ST_JUMPING] && a->vy < 0 && !input_held(p->jump_button)) {
        a->vy /= 2;                                     // let go early = smaller hop
        st[ST_JUMPING] = 0;
    }
    if (a->vy >= 0) st[ST_JUMPING] = 0;
    if (a->on_ground && !a->was_on_ground) actor_sound(a, SND_LAND);

    // Animation.
    if (!a->anim_lock) {
        if (!a->on_ground) actor_play_slot(a, a->vy < 0 ? ANIM_JUMP : ANIM_FALL);
        else if (dx) actor_play_slot(a, ANIM_RUN);
        else actor_play_slot(a, ANIM_IDLE);
    }
}
