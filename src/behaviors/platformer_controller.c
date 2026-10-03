// platformer_controller - side-view running, jumping (variable height) and ladder climbing,
// plus moves unlocked by game variables: dash, wall slide / wall jump and double jump.
#include "platformer_controller.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/physics.h"
#include "engine/vars.h"

enum { ST_COYOTE, ST_JUMP_V, ST_BUFFER, ST_JUMPING, ST_DASH, ST_AIR, ST_WALL };

#define COYOTE_TICKS 6      // can still jump this long after walking off a ledge
#define BUFFER_TICKS 6      // a jump pressed this long before landing still counts
#define WALL_LOCK    10     // after a wall jump the D-pad is ignored this long (pushes off the wall)
#define WALL_GRACE   6      // a jump this soon after letting go of a wall is still a wall jump

// ST_AIR bits: moves used since the last time it stood on the ground (or clung to a wall).
#define AIR_JUMPED   1
#define AIR_DASHED   2

// ST_DASH: dash ticks left (bits 0-7), cooldown (bits 8-15), dashing left (bit 16).
#define DASH_TIMER(d)    ((d) & 0xFF)
#define DASH_COOL(d)     (((d) >> 8) & 0xFF)
#define DASH_LEFT(d)     (((d) >> 16) & 1)
#define DASH_PACK(t, c, l) ((u32)(t) | ((u32)(c) << 8) | ((u32)(l) << 16))

// ST_WALL: wall-jump lock ticks (bits 0-7), side of the last wall clung to (bits 8-9: 1 = right,
// 2 = left), grace ticks left since letting go of it (bits 10-13).
#define WALL_LOCK_OF(w)  ((w) & 0xFF)
#define WALL_SIDE(w)     (((w) >> 8) & 3)
#define WALL_GRACE_OF(w) (((w) >> 10) & 15)

static int unlocked(s16 var) { return var >= 0 && vars_get(var) != 0; }

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

// A solid wall right beside the hitbox (side -1 = left, 1 = right), from head to feet.
static int wall_beside(const Actor *a, int side) {
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    s32 x = side < 0 ? l - 1 : r + 1;
    return (physics_tile_at(x, t + 2) & TILE_SOLID) && (physics_tile_at(x, b - 2) & TILE_SOLID);
}

// A slash or spell animation is still playing: leave the animation alone until it ends.
static int busy_anim(const Actor *a) {
    u8 s = a->anim_slot;
    return (s == ANIM_ATTACK || s == ANIM_ATTACK_UP || s == ANIM_ATTACK_DOWN || s == ANIM_CAST) &&
           !a->anim.done;
}

void platformer_refresh_air(Actor *a) {
    int i = actor_logic_index(a, BHV_PLATFORMER_CONTROLLER);
    if (i >= 0) actor_state_of(a, i)[ST_AIR] = 0;
}

int platformer_dashing(Actor *a) {
    int i = actor_logic_index(a, BHV_PLATFORMER_CONTROLLER);
    return i >= 0 && DASH_TIMER(actor_state_of(a, i)[ST_DASH]) != 0;
}

void bhv_platformer_controller_update(Actor *a, const void *params) {
    const Params_platformer_controller *p = params;
    u32 *st = bhv_state(a);
    int dx = input_dir_x(), dy = input_dir_y();
    fixed jump_v = (fixed)st[ST_JUMP_V];
    int on_ladder = (a->tile_touch & TILE_LADDER) != 0;
    u32 dash = st[ST_DASH];
    u32 dash_t = DASH_TIMER(dash), dash_cool = DASH_COOL(dash), dash_left = DASH_LEFT(dash);
    u32 wall_lock = WALL_LOCK_OF(st[ST_WALL]);
    int wall_side = (int)WALL_SIDE(st[ST_WALL]);         // 1 = right, 2 = left, 0 = none
    u32 wall_grace = WALL_GRACE_OF(st[ST_WALL]);
    if (dash_cool) dash_cool--;
    if (wall_lock) wall_lock--;
    if (a->on_ground) st[ST_AIR] = 0;

    // Dashing: a straight, gravity-free burst. A hit or a wall ends it early.
    if (dash_t) {
        if (a->anim_slot == ANIM_HURT || a->hit_wall || --dash_t == 0) {
            dash_t = 0;
            a->gravity = a->body ? a->body->gravity : 1;
            if (a->anim_slot != ANIM_HURT) a->vx = (dash_left ? -1 : 1) * p->speed;
        } else {
            a->vx = (dash_left ? -1 : 1) * p->dash_speed;
            a->vy = 0;
            st[ST_DASH] = DASH_PACK(dash_t, dash_cool, dash_left);
            st[ST_WALL] = 0;
            return;
        }
    }
    if (unlocked(p->dash_var) && !dash_cool && !a->anim_lock && !a->climbing &&
        !(st[ST_AIR] & AIR_DASHED) && input_pressed(p->dash_button)) {
        dash_left = wall_grace ? wall_side == 1 : dx ? dx < 0 : a->facing_left;
        dash_t = (u32)p->dash_ticks;
        dash_cool = (u32)(p->dash_ticks + p->dash_cooldown);
        if (!a->on_ground) st[ST_AIR] |= AIR_DASHED;
        a->gravity = 0;
        a->vy = 0;
        a->vx = (dash_left ? -1 : 1) * p->dash_speed;
        a->facing_left = (u8)dash_left;
        actor_play_slot(a, ANIM_DASH);
        anim_start(&a->anim, a->anim.anim, a);
        actor_sound(a, SND_DASH);
        st[ST_DASH] = DASH_PACK(dash_t, dash_cool, dash_left);
        st[ST_WALL] = 0;
        return;
    }
    st[ST_DASH] = DASH_PACK(0, dash_cool, dash_left);

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
            st[ST_WALL] = 0;
            return;
        }
    } else if ((on_ladder && dy < 0) || (dy > 0 && (ladder_below(a) || (on_ladder && !a->on_ground)))) {
        a->climbing = 1;
        a->gravity = 0;
        a->vx = a->vy = 0;
        st[ST_WALL] = 0;
        return;
    }

    // Running (knockback and a fresh wall jump keep control away for a moment).
    if (!a->anim_lock && !wall_lock) {
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

    // Wall slide: in the air against a wall, holding toward it (or already clinging and not
    // pushing away). Clinging refreshes the double jump and the air dash. For a few ticks after
    // letting go a jump still kicks off that wall (players press away + jump together).
    int cling = 0, prev = wall_side == 1 ? 1 : wall_side == 2 ? -1 : 0;
    if (unlocked(p->wall_jump_var) && !a->on_ground && !a->anim_lock && a->vy >= 0) {
        if (dx && wall_beside(a, dx)) cling = dx;
        else if (prev && dx != -prev && wall_beside(a, prev)) cling = prev;
    }
    if (cling) {
        wall_side = cling > 0 ? 1 : 2;
        wall_grace = WALL_GRACE;
        if (a->vy > p->wall_slide_speed) a->vy = p->wall_slide_speed;
        a->facing_left = cling > 0;                     // looks away from the wall
        st[ST_AIR] = 0;
    } else if (wall_grace && !a->on_ground) {
        wall_grace--;                                   // just let go: remember the wall a moment
    } else {
        wall_side = 0;
        wall_grace = 0;
    }
    int kick = cling ? cling : wall_grace ? prev : 0;   // wall a jump would kick off

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
    } else if (st[ST_BUFFER] && kick) {                 // wall jump: up and away from the wall
        a->vy = -jump_v;
        a->vx = -kick * p->speed * 3 / 2;
        a->facing_left = kick > 0;
        wall_lock = WALL_LOCK;
        wall_side = 0;
        wall_grace = 0;
        cling = 0;
        st[ST_BUFFER] = 0;
        st[ST_JUMPING] = 1;
        actor_sound(a, SND_JUMP);
    } else if (input_pressed(p->jump_button) && !a->on_ground && !a->climbing && !a->anim_lock &&
               unlocked(p->double_jump_var) && !(st[ST_AIR] & AIR_JUMPED)) {
        a->vy = -jump_v * 7 / 8;                        // double jump: a second, slightly smaller jump
        st[ST_AIR] |= AIR_JUMPED;
        st[ST_BUFFER] = 0;
        st[ST_JUMPING] = 1;
        actor_sound(a, SND_JUMP);
        if (!busy_anim(a)) {
            actor_play_slot(a, ANIM_JUMP);
            anim_start(&a->anim, a->anim.anim, a);      // flap again even if already jumping
        }
    }
    if (st[ST_JUMPING] && a->vy < 0 && !input_held(p->jump_button)) {
        a->vy /= 2;                                     // let go early = smaller hop
        st[ST_JUMPING] = 0;
    }
    if (a->vy >= 0) st[ST_JUMPING] = 0;
    if (a->on_ground && !a->was_on_ground) actor_sound(a, SND_LAND);
    st[ST_WALL] = wall_lock | ((u32)wall_side << 8) | (wall_grace << 10);

    // Animation.
    if (!a->anim_lock && !busy_anim(a)) {
        if (cling) actor_play_slot(a, ANIM_WALL);
        else if (!a->on_ground) actor_play_slot(a, a->vy < 0 ? ANIM_JUMP : ANIM_FALL);
        else if (dx) actor_play_slot(a, ANIM_RUN);
        else actor_play_slot(a, ANIM_IDLE);
    }
}
