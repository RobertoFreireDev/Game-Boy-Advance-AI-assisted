// monster_ai - a wild monster's mind: it wanders around its home, turns hostile when the player
// comes close (only a few at once), walks up and uses its first move on a timer, and walks
// home when the player gets away. Can sleep until a variable reaches a value (a boss).
#include "monster_ai.h"
#include "monster.h"
#include "moves.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/core.h"
#include "engine/actions.h"
#include "engine/particles.h"

enum { ST_MODE, ST_TIMER, ST_DIR, ST_CHARGE, ST_STUCK };
enum { MODE_SLEEP, MODE_WANDER, MODE_HOSTILE, MODE_RETURN };

#define DIR_REST 8

static const s8 s_dx[9] = { 1, 1, 0, -1, -1, -1, 0, 1, 0 };
static const s8 s_dy[9] = { 0, 1, 1, 1, 0, -1, -1, -1, 0 };

static u32 s_count_frame = 0xFFFFFFFF;
static int s_hostile;

// How many monsters are hostile right now (counted once per frame).
static int hostile_count(void) {
    u32 f = core_frame();
    if (f == s_count_frame) return s_hostile;
    s_count_frame = f;
    s_hostile = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (!actor_alive(o)) continue;
        int k = actor_logic_index(o, BHV_MONSTER_AI);
        if (k >= 0 && actor_state_of(o, k)[ST_MODE] == MODE_HOSTILE) s_hostile++;
    }
    return s_hostile;
}

// Walk toward a point in 8 directions; returns 1 when there (within 2 px).
static int walk_to(Actor *a, s32 x, s32 y, fixed speed) {
    s32 dx = x - fx_to_int(a->x), dy = y - fx_to_int(a->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    if (ax <= 2 && ay <= 2) return 1;
    int sx = ax * 2 >= ay && ax > 1 ? fx_sign(dx) : 0;
    int sy = ay * 2 >= ax && ay > 1 ? fx_sign(dy) : 0;
    a->vx = sx * speed;
    a->vy = sy * speed;
    if (sx && sy) {                             // same speed on diagonals
        a->vx = a->vx * 181 / 256;
        a->vy = a->vy * 181 / 256;
    }
    actor_face(a, sx, sy);
    actor_play_slot(a, ANIM_WALK);
    return 0;
}

static void stand(Actor *a) {
    a->vx = a->vy = 0;
    if (!a->anim_lock) actor_play_slot(a, ANIM_IDLE);
}

// Look at a point (8 directions), without moving.
static void face_point(Actor *a, s32 x, s32 y) {
    s32 dx = x - fx_to_int(a->x), dy = y - fx_to_int(a->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    actor_face(a, ax * 2 >= ay ? fx_sign(dx) : 0, ay * 2 >= ax ? fx_sign(dy) : 0);
}

static void wander(Actor *a, const Params_monster_ai *p, u32 *st) {
    if (st[ST_TIMER] == 0 || a->hit_wall) {
        if (st[ST_DIR] != DIR_REST) {               // rest often
            st[ST_DIR] = DIR_REST;
            st[ST_TIMER] = (u32)rng_range(40, 120);
        } else {
            u32 d = (u32)rng_range(0, 7);
            s32 ox = fx_to_int(a->x) - a->home_x, oy = fx_to_int(a->y) - a->home_y;
            if (fx_abs(ox) > p->roam || fx_abs(oy) > p->roam) {    // too far: back toward home
                int sx = -fx_sign(ox), sy = -fx_sign(oy);
                for (d = 0; d < 8 && (s_dx[d] != sx || s_dy[d] != sy); d++) {}
            }
            st[ST_DIR] = d;
            st[ST_TIMER] = (u32)rng_range(20, 60);
        }
    } else {
        st[ST_TIMER]--;
    }
    int d = (int)st[ST_DIR];
    if (d == DIR_REST) { stand(a); return; }
    a->vx = s_dx[d] * p->speed;
    a->vy = s_dy[d] * p->speed;
    if (s_dx[d] && s_dy[d]) {
        a->vx = a->vx * 181 / 256;
        a->vy = a->vy * 181 / 256;
    }
    actor_face(a, s_dx[d], s_dy[d]);
    actor_play_slot(a, ANIM_WALK);
}

int monster_ai_asleep(const Actor *a) {
    int k = actor_logic_index(a, BHV_MONSTER_AI);
    return k >= 0 && actor_state_of((Actor *)a, k)[ST_MODE] == MODE_SLEEP;
}

void bhv_monster_ai_init(Actor *a, const void *params) {
    const Params_monster_ai *p = params;
    u32 *st = bhv_state(a);
    st[ST_MODE] = p->wake_var >= 0 ? MODE_SLEEP : MODE_WANDER;
    st[ST_TIMER] = (u32)rng_range(0, 60);
    st[ST_DIR] = DIR_REST;
    st[ST_CHARGE] = 0;
    st[ST_STUCK] = 0;
}

void bhv_monster_ai_update(Actor *a, const void *params) {
    const Params_monster_ai *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_MODE] == MODE_SLEEP) {
        stand(a);
        if (vars_get(p->wake_var) >= p->wake_at) {
            st[ST_MODE] = MODE_WANDER;
            scripts_start(p->on_wake, a);
        } else if (p->sleep_effect && (core_frame() & 63) == 0) {
            particles_burst(p->sleep_effect, fx_to_int(a->x) + 6, fx_to_int(a->y) - 16);
        }
        return;
    }
    if (!monster_can_act(a) || a->anim_lock) {      // paralyzed, frozen or mid-attack
        a->vx = a->vy = 0;
        return;
    }
    const SpeciesData *sp = monster_species(a);
    const MoveData *m = sp ? sp->moves[0] : NULL;
    Actor *pl = actor_player();
    int seen = pl && monster_targetable(pl);
    int dist = seen ? moves_tiles(a, pl) : 999;
    switch (st[ST_MODE]) {
    case MODE_WANDER:
        if (seen && dist <= p->sight && hostile_count() < p->max_hostile) {
            st[ST_MODE] = MODE_HOSTILE;
            st[ST_CHARGE] = (u32)p->charge / 2;     // first attack comes a bit sooner
            s_hostile++;
            break;
        }
        wander(a, p, st);
        break;
    case MODE_HOSTILE: {
        if (!pl || !actor_alive(pl) || dist > p->give_up) {
            st[ST_MODE] = MODE_RETURN;
            break;
        }
        if (st[ST_CHARGE] < (u32)p->charge) st[ST_CHARGE]++;
        if (!seen) { stand(a); break; }             // it flew out of reach: wait
        int reach = m ? moves_reach(m) : 1;
        if (dist > reach) {
            s32 px = fx_to_int(a->x), py = fx_to_int(a->y);
            // Stuck on a wall for a while: try sideways for a moment.
            if (st[ST_STUCK] > 30) {
                if (st[ST_STUCK]++ > 60) st[ST_STUCK] = 0;
                walk_to(a, px + (fx_to_int(pl->y) - py), py - (fx_to_int(pl->x) - px), p->chase_speed);
                break;
            }
            walk_to(a, fx_to_int(pl->x), fx_to_int(pl->y), p->chase_speed);
            if (a->x == a->prev_x && a->y == a->prev_y) st[ST_STUCK]++;
            else st[ST_STUCK] = 0;
            break;
        }
        stand(a);
        face_point(a, fx_to_int(pl->x), fx_to_int(pl->y));
        if (st[ST_CHARGE] >= (u32)p->charge && m && moves_use(a, m)) st[ST_CHARGE] = 0;
        break;
    }
    case MODE_RETURN:
        if (seen && dist <= p->sight && hostile_count() < p->max_hostile) {
            st[ST_MODE] = MODE_HOSTILE;
            s_hostile++;
            break;
        }
        if (walk_to(a, a->home_x, a->home_y, p->speed) || a->hit_wall) {
            if (a->hit_wall && (core_frame() & 31)) break;      // keep trying a little
            st[ST_MODE] = MODE_WANDER;
            st[ST_TIMER] = 0;
            st[ST_DIR] = DIR_REST;
        }
        break;
    }
}
