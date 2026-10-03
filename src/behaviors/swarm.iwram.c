// swarm.iwram.c (hot code: ARM in IWRAM) - horde movement (top-down): head straight for the
// player from any direction and push away from other members of the same category so crowds
// spread out instead of stacking into one sprite. Members left far behind jump ahead of the
// player, off screen, so the horde keeps up and the actor pool is never wasted on stragglers.
//
// Crowding uses a small occupancy grid instead of comparing every pair: the world is cut into
// 16x16-pixel cells, folded onto a 32x32 table (it wraps every 512 pixels; the real distance
// check sorts out far-away cells that share a slot). Each member writes "I am here, frame F"
// into its cell, then looks at the 3x3 cells around it: 9 table reads per member per tick.
// Marks older than one frame are ignored, so the table never needs clearing.
#include "swarm.h"
#include "behavior_params.h"
#include "engine/core.h"
#include "engine/bg.h"

enum { ST_PUSH_X, ST_PUSH_Y, ST_UX, ST_UY };

#define SEPARATION  12              // pixels: closer than this to a neighbour = push apart
#define AIM_EVERY   8               // re-aim at the player every 8 ticks (staggered across the horde)
#define PUSH (FX_ONE * 3 / 4)       // push speed when crowded; it fades out over a few ticks
#define CELL_SHIFT  4               // 16-pixel cells
#define GRID        32              // 32x32 cells (power of two: wraps with a mask)

static u16 s_grid[GRID * GRID];     // (frame & 255) << 8 | (actor index + 1); 0 = empty

static s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline int cell_of(s32 x, s32 y) {
    return (((y >> CELL_SHIFT) & (GRID - 1)) << 5) | ((x >> CELL_SHIFT) & (GRID - 1));
}

void bhv_swarm_init(Actor *a, const void *params) {
    (void)params;
    u32 *st = bhv_state(a);
    st[ST_PUSH_X] = st[ST_PUSH_Y] = st[ST_UX] = st[ST_UY] = 0;
    a->gravity = 0;
}

// Push away from same-category members marked in the 3x3 cells around (x, y), then mark our cell.
static void separate(Actor *a, u32 *st, int idx, s32 x, s32 y) {
    u32 now = core_frame() & 255;
    fixed px = (s32)st[ST_PUSH_X] * 3 / 4, py = (s32)st[ST_PUSH_Y] * 3 / 4;
    for (s32 cy = y - (1 << CELL_SHIFT); cy <= y + (1 << CELL_SHIFT); cy += 1 << CELL_SHIFT) {
        for (s32 cx = x - (1 << CELL_SHIFT); cx <= x + (1 << CELL_SHIFT); cx += 1 << CELL_SHIFT) {
            u32 m = s_grid[cell_of(cx, cy)];
            int j = (int)(m & 255) - 1;
            if (j < 0 || j == idx || ((now - (m >> 8)) & 255) > 1) continue;    // empty, us, or stale
            const Actor *o = &g_actors[j];
            if (!o->active || o->category != a->category || (o->flags & ACTOR_DYING)) continue;
            s32 ox = x - fx_to_int(o->x), oy = y - fx_to_int(o->y);
            if (ox >= SEPARATION || ox <= -SEPARATION || oy >= SEPARATION || oy <= -SEPARATION) continue;
            if (!ox && !oy) ox = (idx & 1) ? 1 : -1;
            px += fx_sign(ox) * PUSH;
            py += fx_sign(oy) * PUSH;
        }
    }
    px = clamp(px, -PUSH, PUSH);
    py = clamp(py, -PUSH, PUSH);
    st[ST_PUSH_X] = (u32)px;
    st[ST_PUSH_Y] = (u32)py;
    a->vx += px;
    a->vy += py;
    s_grid[cell_of(x, y)] = (u16)((now << 8) | (u32)(idx + 1));
}

void bhv_swarm_update(Actor *a, const void *params) {
    const Params_swarm *p = params;
    u32 *st = bhv_state(a);
    Actor *pl = actor_player();
    if (a->anim_lock) return;                   // being knocked back
    if (!pl) {
        a->vx = a->vy = 0;
        actor_play_slot(a, ANIM_IDLE);
        return;
    }
    s32 dx = fx_to_int(pl->x - a->x), dy = fx_to_int(pl->y - a->y);

    // Far behind: reappear ahead of the player (mirrored through the player, off screen).
    if (p->leash > 0 && (fx_abs(dx) > p->leash || fx_abs(dy) > p->leash * 3 / 4)) {
        s32 nx = clamp(fx_to_int(pl->x) + dx * 2 / 3, 8, bg_world_w() - 8);
        s32 ny = clamp(fx_to_int(pl->y) + dy * 2 / 3, 16, bg_world_h() - 8);
        a->x = a->prev_x = fx_from_int(nx);
        a->y = a->prev_y = fx_from_int(ny);
        a->vx = a->vy = 0;
        return;
    }

    // Direction to the player as a unit vector: the square root and divisions only run every
    // AIM_EVERY ticks per member (or when it has no direction yet), which keeps big hordes cheap.
    int idx = actor_index(a);
    if (((core_frame() + (u32)idx) % AIM_EVERY) == 0 || (!st[ST_UX] && !st[ST_UY])) {
        s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
        st[ST_UX] = (u32)(len < 2 ? 0 : dx * FX_ONE / len);
        st[ST_UY] = (u32)(len < 2 ? 0 : dy * FX_ONE / len);
    }
    fixed ux = (s32)st[ST_UX], uy = (s32)st[ST_UY];
    a->vx = fx_mul(p->speed, ux);
    a->vy = fx_mul(p->speed, uy);
    if (p->wobble) {                            // weave side to side (flying things)
        fixed w = fx_sin_deg((s32)(core_frame() * 6) + idx * 47) / 2;
        a->vx -= fx_mul(w, uy);
        a->vy += fx_mul(w, ux);
    }

    separate(a, st, idx, fx_to_int(a->x), fx_to_int(a->y));

    if (dx) a->facing_left = dx < 0;
    actor_play_slot(a, ANIM_WALK);
}
