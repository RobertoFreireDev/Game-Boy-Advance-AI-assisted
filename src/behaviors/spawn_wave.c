// spawn_wave - brings in enemies just off screen around the camera, during a time window of
// the run (read from a seconds variable). Formations: scatter (each one on a random side),
// group (a pack from one side) or ring (a circle closing in on the player). A big wave appears
// over a few ticks (at most SPAWN_PER_TICK per tick) so it never stalls a frame.
#include "spawn_wave.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/scene.h"
#include "engine/camera.h"
#include "engine/bg.h"
#include "engine/actions.h"

enum { ST_TIMER, ST_DONE, ST_PENDING, ST_GROUP, ST_BASE };

#define SPAWN_PER_TICK 3

#define RING_RADIUS 150             // just outside the screen corners (half diagonal = 144)
#define GROUP_JITTER 14

static s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }

static int inside_world(s32 x, s32 y) {
    return x >= 8 && x < bg_world_w() - 8 && y >= 16 && y < bg_world_h() - 4;
}

// A point just outside the visible screen, inside the world if possible.
static void offscreen_point(s32 *x, s32 *y) {
    s32 cx = camera_x(), cy = camera_y();
    for (int tries = 0; tries < 6; tries++) {
        switch (rng_range(0, 3)) {
        case 0:  *x = cx - 12;       *y = cy + rng_range(0, 175); break;   // left
        case 1:  *x = cx + 240 + 12; *y = cy + rng_range(0, 175); break;   // right
        case 2:  *x = cx + rng_range(0, 239); *y = cy - 4;        break;   // top (feet just above)
        default: *x = cx + rng_range(0, 239); *y = cy + 160 + 20; break;   // bottom
        }
        if (inside_world(*x, *y)) return;
    }
    *x = clamp(*x, 8, bg_world_w() - 9);
    *y = clamp(*y, 16, bg_world_h() - 5);
}

void bhv_spawn_wave_update(Actor *a, const void *params) {
    const Params_spawn_wave *p = params;
    u32 *st = bhv_state(a);
    s32 t = vars_get(p->clock_var);
    if (t < p->start || (p->end > 0 && t >= p->end) || (p->once && st[ST_DONE])) return;
    if (st[ST_TIMER]) {
        st[ST_TIMER]--;
    } else {                                    // a new wave: queue it
        st[ST_TIMER] = (u32)p->every;
        st[ST_PENDING] = (u32)p->count;
        s32 gx, gy;
        offscreen_point(&gx, &gy);
        st[ST_GROUP] = (u32)(gx & 0xFFFF) | ((u32)gy << 16);
        st[ST_BASE] = (u32)rng_range(0, 359);
    }
    if (!st[ST_PENDING]) return;

    int alive = actor_count_node(p->object), free = actor_free_slots(), spawned = 0;
    const Actor *pl = actor_player();
    s32 px = pl ? fx_to_int(pl->x) : camera_x() + 120, py = pl ? fx_to_int(pl->y) : camera_y() + 88;
    s32 gx = (s16)(st[ST_GROUP] & 0xFFFF), gy = (s16)(st[ST_GROUP] >> 16);
    while (st[ST_PENDING] && spawned < SPAWN_PER_TICK) {
        if (alive >= p->max_alive || free <= p->reserve) {
            st[ST_PENDING] = 0;                 // full: the rest of this wave is skipped
            break;
        }
        int i = p->count - (int)st[ST_PENDING];
        s32 x, y;
        switch (p->formation) {
        case SPAWN_WAVE_FORMATION_GROUP:
            x = clamp(gx + rng_range(-GROUP_JITTER, GROUP_JITTER), 8, bg_world_w() - 9);
            y = clamp(gy + rng_range(-GROUP_JITTER, GROUP_JITTER), 16, bg_world_h() - 5);
            break;
        case SPAWN_WAVE_FORMATION_RING: {
            s32 ang = (s32)st[ST_BASE] + i * 360 / p->count;
            x = clamp(px + fx_to_int(fx_cos_deg(ang) * RING_RADIUS), 8, bg_world_w() - 9);
            y = clamp(py - fx_to_int(fx_sin_deg(ang) * RING_RADIUS), 16, bg_world_h() - 5);
            break;
        }
        default:
            offscreen_point(&x, &y);
            break;
        }
        st[ST_PENDING]--;
        if (!scene_spawn(p->object, x, y)) {
            st[ST_PENDING] = 0;
            break;
        }
        alive++;
        free--;
        spawned++;
    }
    if (spawned && !st[ST_DONE]) {
        st[ST_DONE] = 1;
        scripts_start(p->on_spawn, a);
    }
}
