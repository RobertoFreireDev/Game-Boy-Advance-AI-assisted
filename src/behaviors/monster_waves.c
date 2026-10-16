// monster_waves - a monster-battle run in waves. Each wave's monsters appear on the map's outer
// edge (the dark band around an arena), out of the camera's view when they can, and walk in to
// hunt the player. Wave w brings count + count_add x (w - 1) of them (at most max_count), each at
// a random level from level + level_add x (w - 1) to that + level_spread, with hp + hp_per_level x
// level hit points; later waves can bring other versions of the monster (object_2, object_3: more
// moves); the first early_waves waves bring no monster of the early_skip types (those that attack
// from far away). The last wave also brings the boss, first. With max_alive, at most that many are out at
// once (25 hunting monsters cost more than a frame) and the rest walk in as others faint. When a
// wave's last monster has fainted the next one comes after `pause` ticks; after the last wave,
// on_clear runs. At most SPAWN_PER_TICK appear per tick, so a big wave never stalls a frame.
#include "monster_waves.h"
#include "monster.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/scene.h"
#include "engine/camera.h"
#include "engine/bg.h"
#include "engine/actions.h"

enum { ST_WAVE, ST_PHASE, ST_TIMER, ST_PENDING, ST_BOSS };
enum { PH_WAIT, PH_SPAWN, PH_FIGHT, PH_DONE };

#define SPAWN_PER_TICK 2
#define EDGE_PX 16          // spawn points: a 16x16 body inside the map's outermost 16 px
#define VIEW_MARGIN 8       // ... and at least this far outside the camera's view

static s32 round_fx(fixed v) { return (v + FX_ONE / 2) >> FX_SHIFT; }

// How many monsters wave w (from 1) brings, without the boss.
static int wave_count(const Params_monster_waves *p, int w) {
    s32 n = round_fx(fx_from_int(p->count) + p->count_add * (w - 1));
    if (n > p->max_count) n = p->max_count;
    return n < 1 ? 1 : (int)n;
}

// The lowest level of wave w's monsters (the highest is level_spread more).
static int wave_level(const Params_monster_waves *p, int w) {
    s32 lv = round_fx(fx_from_int(p->level) + p->level_add * (w - 1));
    return lv < 1 ? 1 : (int)lv;
}

// Which version of the monster wave w brings (later waves: more moves).
static s16 wave_object(const Params_monster_waves *p, int w) {
    if (p->object_3 >= 0 && p->object_3_from > 0 && w >= p->object_3_from) return p->object_3;
    if (p->object_2 >= 0 && p->object_2_from > 0 && w >= p->object_2_from) return p->object_2;
    return p->object;
}

static int alive(const Params_monster_waves *p) {
    int n = actor_count_node(p->object);
    if (p->object_2 >= 0) n += actor_count_node(p->object_2);
    if (p->object_3 >= 0) n += actor_count_node(p->object_3);
    if (p->boss >= 0) n += actor_count_node(p->boss);
    return n;
}

// Actions that run as the player (words pop up over it), or as the director without one.
static void run(const ActionList *l, Actor *self) {
    Actor *pl = actor_player();
    scripts_start(*l, pl ? pl : self);
}

// A point on the map's outer edge, where a monster's feet go: its body inside the outermost
// EDGE_PX pixels, out of the camera's view when one of a few tries finds such a spot.
static void edge_point(s32 *x, s32 *y) {
    s32 w = bg_world_w(), h = bg_world_h();
    s32 cx = camera_x() - VIEW_MARGIN, cy = camera_y() - VIEW_MARGIN;
    s32 cr = camera_x() + 240 + VIEW_MARGIN, cb = camera_y() + 160 + VIEW_MARGIN;
    for (int tries = 0; tries < 8; tries++) {
        s32 k = rng_range(0, (w + h) * 2 - 1);     // a spot along the whole edge, each as likely
        if (k < w)              { *x = k;                 *y = EDGE_PX - 1; }        // top
        else if (k < w * 2)     { *x = k - w;             *y = h - 1; }              // bottom
        else if (k < w * 2 + h) { *x = EDGE_PX / 2;       *y = k - w * 2; }          // left
        else                    { *x = w - EDGE_PX / 2;   *y = k - w * 2 - h; }      // right
        if (*x < EDGE_PX / 2) *x = EDGE_PX / 2;
        if (*x > w - EDGE_PX / 2) *x = w - EDGE_PX / 2;
        if (*y < EDGE_PX - 1) *y = EDGE_PX - 1;
        if (*y > h - 1) *y = h - 1;
        // its body: x - 8 .. x + 7, y - 15 .. y
        if (*x + 7 < cx || *x - 8 > cr || *y < cy || *y - 15 > cb) return;
    }
}

static void start_wave(Actor *a, const Params_monster_waves *p, u32 *st) {
    st[ST_WAVE]++;
    st[ST_PHASE] = PH_SPAWN;
    st[ST_PENDING] = (u32)wave_count(p, (int)st[ST_WAVE]);
    st[ST_BOSS] = st[ST_WAVE] >= (u32)p->waves && p->boss >= 0;
    vars_set(p->wave_var, (s32)st[ST_WAVE]);
    run(&p->on_wave, a);
}

void bhv_monster_waves_init(Actor *a, const void *params) {
    const Params_monster_waves *p = params;
    u32 *st = bhv_state(a);
    st[ST_WAVE] = 0;
    st[ST_PHASE] = PH_WAIT;
    st[ST_TIMER] = (u32)p->start_delay;
    st[ST_PENDING] = 0;
    st[ST_BOSS] = 0;
    vars_set(p->wave_var, 0);
}

void bhv_monster_waves_update(Actor *a, const void *params) {
    const Params_monster_waves *p = params;
    u32 *st = bhv_state(a);
    switch (st[ST_PHASE]) {
    case PH_WAIT:
        if (st[ST_TIMER]) st[ST_TIMER]--;
        else start_wave(a, p, st);
        break;
    case PH_SPAWN: {
        int w = (int)st[ST_WAVE], lo = wave_level(p, w);
        for (int k = 0; k < SPAWN_PER_TICK && (st[ST_PENDING] || st[ST_BOSS]); k++) {
            if (p->max_alive > 0 && alive(p) >= p->max_alive) break;   // the rest wait for room
            s32 x, y;
            edge_point(&x, &y);
            if (st[ST_BOSS]) {                      // the boss leads the last wave in
                Actor *m = scene_spawn(p->boss, x, y);
                if (!m) break;                      // the pool is full: try again next tick
                monster_set_level(m, p->boss_level, p->boss_hp);
                st[ST_BOSS] = 0;
                run(&p->on_boss, a);
            } else {
                monster_skip_types(w <= p->early_waves ? p->early_skip : 0);   // no far reachers at first
                Actor *m = scene_spawn(wave_object(p, w), x, y);
                monster_skip_types(0);
                if (!m) break;
                int lv = rng_range(lo, lo + p->level_spread);
                monster_set_level(m, lv, p->hp + p->hp_per_level * lv);
                st[ST_PENDING]--;
            }
        }
        if (!st[ST_PENDING] && !st[ST_BOSS]) st[ST_PHASE] = PH_FIGHT;
        break;
    }
    case PH_FIGHT:
        if (alive(p)) break;
        if (st[ST_WAVE] >= (u32)p->waves) {
            st[ST_PHASE] = PH_DONE;
            run(&p->on_clear, a);
        } else {
            st[ST_PHASE] = PH_WAIT;
            st[ST_TIMER] = (u32)p->pause;
        }
        break;
    }
    if (st[ST_PHASE] != PH_DONE)
        vars_set(p->left_var, alive(p) + (s32)st[ST_PENDING] + (s32)st[ST_BOSS]);
    else
        vars_set(p->left_var, 0);
}
