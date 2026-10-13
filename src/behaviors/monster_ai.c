// monster_ai - a wild monster's mind: it wanders around its home, turns hostile when the player
// comes close (only a few at once), walks up and uses its first move on a timer, and walks
// home when the player gets away. It keeps some room from the other wild monsters, so a pack
// spreads out. Can sleep until a variable reaches a value (a boss). A boss can also use its
// moves 2 and 3: each recharges on its own timer, and an attack is the highest one ready
// (they can hit at their own power and recharge slower than the player's).
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
#define SPREAD_HOSTILE 3        // looks at its neighbors every 4th frame while hostile (a power of 2, minus 1)
#define SPREAD_CALM 15          // and every 16th while wandering or going home (each monster on its own frame)

static const s8 s_dx[9] = { 1, 1, 0, -1, -1, -1, 0, 1, 0 };
static const s8 s_dy[9] = { 0, 1, 1, 1, 0, -1, -1, -1, 0 };

static u32 s_count_frame = 0xFFFFFFFF;
static int s_hostile;           // hostile right now: last frame's count + those that turned this frame
static int s_hostile_next;      // counted this frame, for the next one
// The crowd: each solid monster with this behavior writes its pool index and hitbox here when
// it updates; everyone reads the list of the frame before (complete, a frame old at most).
typedef struct { s16 l, t, r, b; u8 who; } Spot;
EWRAM_BSS static Spot s_crowd[2][MAX_ACTORS];
static u8 s_crowd_n[2], s_fill;                 // s_crowd[s_fill] is being written this frame
EWRAM_BSS static s8 s_away[MAX_ACTORS][2];      // per pool index: its way out of the crowd (-1..1 each)
EWRAM_BSS static u16 s_extra[MAX_ACTORS][2];    // per pool index: ticks since moves 2 and 3 were used
static int s_me;                                // pool index of the monster updating now

// A new frame: last frame's list and hostile count become the ones everybody reads.
static void new_frame(void) {
    u32 f = core_frame();
    if (f == s_count_frame) return;
    s_count_frame = f;
    s_fill ^= 1;
    s_crowd_n[s_fill] = 0;
    s_hostile = s_hostile_next;
    s_hostile_next = 0;
}

// This monster joins the crowd list of this frame (and the hostile count).
static void join(const Actor *a, u32 mode) {
    if (mode == MODE_HOSTILE) s_hostile_next++;
    if (a->solid_mode == SOLID_NONE) return;     // flying or phasing: passes over the others
    Spot *o = &s_crowd[s_fill][s_crowd_n[s_fill]++];
    o->l = (s16)(fx_to_int(a->x) + a->hb_x);
    o->t = (s16)(fx_to_int(a->y) + a->hb_y);
    o->r = (s16)(o->l + a->hb_w - 1);
    o->b = (s16)(o->t + a->hb_h - 1);
    o->who = (u8)s_me;
}

// A monster turns hostile: count it now and in the next frame's count.
static void turn_hostile(u32 *st) {
    st[ST_MODE] = MODE_HOSTILE;
    s_hostile++;
    s_hostile_next++;
}

// Which way (8 directions) leads away from the monsters whose bodies are less than `gap`
// pixels from this one's; 0, 0 when none is that close.
static void find_away(const Actor *a, int gap, s8 *out) {
    s32 l = fx_to_int(a->x) + a->hb_x, t = fx_to_int(a->y) + a->hb_y;
    s32 r = l + a->hb_w - 1, b = t + a->hb_h - 1;
    s32 sx = 0, sy = 0;
    const Spot *o = s_crowd[s_fill ^ 1];
    for (int k = s_crowd_n[s_fill ^ 1]; k > 0; k--, o++) {
        if (o->l > r + gap || l > o->r + gap || o->t > b + gap || t > o->b + gap || o->who == s_me) continue;
        s32 dx = (l + r) - (o->l + o->r), dy = (t + b) - (o->t + o->b);
        if (!dx && !dy) dx = s_me < o->who ? -1 : 1;    // right on top of each other: split by index
        s32 ax = fx_abs(dx), ay = fx_abs(dy);
        if (ax * 2 >= ay) sx += fx_sign(dx);
        if (ay * 2 >= ax) sy += fx_sign(dy);
    }
    out[0] = (s8)fx_sign(sx);
    out[1] = (s8)fx_sign(sy);
}

// Bend a step (sx, sy) out of the crowd: add the way away from the close ones; when the two
// cancel out (a monster right in the way) go around it, each monster to its own side.
static void steer(int *sx, int *sy) {
    const s8 *w = s_away[s_me];
    if (!w[0] && !w[1]) return;
    int x = fx_sign(*sx + w[0]), y = fx_sign(*sy + w[1]);
    if (!x && !y) {
        int side = (s_me & 1) ? 1 : -1;
        x = -*sy * side;
        y = *sx * side;
    }
    *sx = x;
    *sy = y;
}

// Set the speed for a step in 8 directions (same speed on diagonals).
static void go(Actor *a, int sx, int sy, fixed speed) {
    a->vx = sx * speed;
    a->vy = sy * speed;
    if (sx && sy) {
        a->vx = a->vx * 181 / 256;
        a->vy = a->vy * 181 / 256;
    }
}

// Walk toward a point in 8 directions, around the crowd; returns 1 when there (within 2 px).
static int walk_to(Actor *a, s32 x, s32 y, fixed speed) {
    s32 dx = x - fx_to_int(a->x), dy = y - fx_to_int(a->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    if (ax <= 2 && ay <= 2) return 1;
    int sx = ax * 2 >= ay && ax > 1 ? fx_sign(dx) : 0;
    int sy = ay * 2 >= ax && ay > 1 ? fx_sign(dy) : 0;
    steer(&sx, &sy);
    go(a, sx, sy, speed);
    actor_face(a, sx, sy);
    actor_play_slot(a, ANIM_WALK);
    return 0;
}

// Hold still, or step out of the crowd when another monster stands too close.
static void stand(Actor *a, fixed speed) {
    const s8 *w = s_away[s_me];
    if ((w[0] || w[1]) && !a->anim_lock) {
        go(a, w[0], w[1], speed);
        actor_face(a, w[0], w[1]);
        actor_play_slot(a, ANIM_WALK);
        return;
    }
    a->vx = a->vy = 0;
    if (!a->anim_lock) actor_play_slot(a, ANIM_IDLE);
}

// In reach of the player and another monster too close: slide sideways around the player
// (across the line to it, toward the free side), so the pack rings it instead of piling up.
static void slide_around(Actor *a, const Actor *pl, fixed speed) {
    const s8 *w = s_away[s_me];
    s32 dx = fx_to_int(pl->x) - fx_to_int(a->x), dy = fx_to_int(pl->y) - fx_to_int(a->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    int cx = ax * 2 >= ay ? fx_sign(dx) : 0, cy = ay * 2 >= ax ? fx_sign(dy) : 0;
    int tx = -cy, ty = cx;                          // one of the two ways across
    int dot = tx * w[0] + ty * w[1];
    if (dot < 0 || (dot == 0 && (s_me & 1))) { tx = -tx; ty = -ty; }
    go(a, tx, ty, speed);
    actor_play_slot(a, ANIM_WALK);
}

// Look at a point (8 directions), without moving.
static void face_point(Actor *a, s32 x, s32 y) {
    s32 dx = x - fx_to_int(a->x), dy = y - fx_to_int(a->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    actor_face(a, ax * 2 >= ay ? fx_sign(dx) : 0, ay * 2 >= ax ? fx_sign(dy) : 0);
}

static void wander(Actor *a, const Params_monster_ai *p, u32 *st, fixed speed) {
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
    if (d == DIR_REST) { stand(a, speed); return; }
    int sx = s_dx[d], sy = s_dy[d];
    steer(&sx, &sy);
    go(a, sx, sy, speed);
    actor_face(a, sx, sy);
    actor_play_slot(a, ANIM_WALK);
}

// How many of its species' moves it uses (1-3).
static int move_count(const Params_monster_ai *p) {
    return p->moves < 1 ? 1 : p->moves > 3 ? 3 : p->moves;
}

// Is its move `i` (1 = move 2, 2 = move 3) recharged? It takes moves_charge % of the player's time.
static int extra_ready(const Params_monster_ai *p, const SpeciesData *sp, int i) {
    return sp->moves[i] && s_extra[s_me][i - 1] >= (u32)sp->moves[i]->charge * (u32)p->moves_charge / 100;
}

// The move it walks up to the player for: its highest ready move 2 or 3 (not one it uses on
// itself, which reaches no one), else move 1.
static const MoveData *aimed_move(const Params_monster_ai *p, const SpeciesData *sp) {
    for (int i = move_count(p) - 1; i >= 1; i--)
        if (extra_ready(p, sp, i) && sp->moves[i]->shape != SHAPE_SELF) return sp->moves[i];
    return sp->moves[0];
}

// One attack: its highest ready move 3 or 2 that goes off, else move 1. Returns 1 if one did.
static int attack(Actor *a, const Params_monster_ai *p, const SpeciesData *sp) {
    for (int i = move_count(p) - 1; i >= 1; i--) {
        if (extra_ready(p, sp, i) && moves_use_power(a, sp->moves[i], 0, p->moves_power)) {
            s_extra[s_me][i - 1] = 0;
            return 1;
        }
    }
    return sp->moves[0] && moves_use(a, sp->moves[0], 0);
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
    s_away[actor_index(a)][0] = s_away[actor_index(a)][1] = 0;
    s_extra[actor_index(a)][0] = s_extra[actor_index(a)][1] = 0xFFFF;   // moves 2 and 3 start ready
}

void bhv_monster_ai_update(Actor *a, const void *params) {
    const Params_monster_ai *p = params;
    u32 *st = bhv_state(a);
    s_me = actor_index(a);
    new_frame();
    join(a, st[ST_MODE]);
    if (p->spread > 0 && ((s_count_frame + (u32)s_me) & (st[ST_MODE] == MODE_HOSTILE ? SPREAD_HOSTILE : SPREAD_CALM)) == 0)
        find_away(a, p->spread * 8, s_away[s_me]);
    if (st[ST_MODE] == MODE_SLEEP) {
        a->vx = a->vy = 0;                          // asleep: it doesn't make room
        if (!a->anim_lock) actor_play_slot(a, ANIM_IDLE);
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
    for (int i = 0; i < 2; i++)                     // moves 2 and 3 recharge while it can act
        if (s_extra[s_me][i] < 0xFFFF) s_extra[s_me][i]++;
    const MoveData *m = sp ? aimed_move(p, sp) : NULL;
    fixed walk = monster_speed(a, p->speed), chase = monster_speed(a, p->chase_speed);   // its species' pace
    Actor *pl = actor_player();
    int seen = pl && monster_targetable(pl);
    int near = 0;                                   // close enough to measure (moves_within)
    if (seen) {                                     // far away (most of them): no need to measure
        s32 fx = fx_abs(fx_to_int(pl->x) - fx_to_int(a->x)), fy = fx_abs(fx_to_int(pl->y) - fx_to_int(a->y));
        near = (fx > fy ? fx : fy) <= (p->give_up + p->sight + 4) * 8;
    }
    switch (st[ST_MODE]) {
    case MODE_WANDER:
        if (near && moves_within(a, pl, p->sight) && s_hostile < p->max_hostile) {
            turn_hostile(st);
            st[ST_CHARGE] = (u32)p->charge / 2;     // first attack comes a bit sooner
            break;
        }
        wander(a, p, st, walk);
        break;
    case MODE_HOSTILE: {
        if (!pl || !actor_alive(pl) || !near || !moves_within(a, pl, p->give_up)) {
            st[ST_MODE] = MODE_RETURN;
            break;
        }
        if (st[ST_CHARGE] < (u32)p->charge) st[ST_CHARGE]++;
        if (!seen) { stand(a, walk); break; }       // it flew out of reach: wait
        int reach = m ? moves_reach(m) : 1;
        if (!moves_within(a, pl, reach)) {
            s32 px = fx_to_int(a->x), py = fx_to_int(a->y);
            // Stuck on a wall for a while: try sideways for a moment.
            if (st[ST_STUCK] > 30) {
                if (st[ST_STUCK]++ > 60) st[ST_STUCK] = 0;
                walk_to(a, px + (fx_to_int(pl->y) - py), py - (fx_to_int(pl->x) - px), chase);
                break;
            }
            walk_to(a, fx_to_int(pl->x), fx_to_int(pl->y), chase);
            if (a->x == a->prev_x && a->y == a->prev_y) st[ST_STUCK]++;
            else st[ST_STUCK] = 0;
            break;
        }
        if (s_away[s_me][0] || s_away[s_me][1]) slide_around(a, pl, walk);
        else stand(a, walk);
        face_point(a, fx_to_int(pl->x), fx_to_int(pl->y));
        if (st[ST_CHARGE] >= (u32)p->charge && m && monster_can_attack(a)) {     // blinded: it waits, charged
            if (attack(a, p, sp)) st[ST_CHARGE] = 0;
            else walk_to(a, fx_to_int(pl->x), fx_to_int(pl->y), chase);  // a beam off its line: step closer
        }
        break;
    }
    case MODE_RETURN:
        if (near && moves_within(a, pl, p->sight) && s_hostile < p->max_hostile) {
            turn_hostile(st);
            break;
        }
        if (walk_to(a, a->home_x, a->home_y, walk) || a->hit_wall) {
            if (a->hit_wall && (core_frame() & 31)) break;      // keep trying a little
            st[ST_MODE] = MODE_WANDER;
            st[ST_TIMER] = 0;
            st[ST_DIR] = DIR_REST;
        }
        break;
    }
}
