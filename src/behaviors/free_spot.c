// free_spot - when it appears with its hitbox on a solid tile (a drop from a monster that
// fainted in an arena's dark edge, where the player can't go), the actor moves to the nearest
// spot where its whole hitbox is clear of solid tiles, up to `reach` pixels away. Runs once.
#include "free_spot.h"
#include "behavior_params.h"
#include "engine/physics.h"
#include "engine/bg.h"

#define STEP 8              // the search grid (pixels): one tile, then refined pixel by pixel

// True if the actor's hitbox, with its origin at (x, y), is inside the map and on no solid
// tile (above and below the map count as empty for physics, so they are ruled out here).
static int clear_at(const Actor *a, s32 x, s32 y) {
    s32 l = x + a->hb_x, t = y + a->hb_y;
    s32 r = l + a->hb_w - 1, b = t + a->hb_h - 1;
    if (l < 0 || t < 0 || r >= bg_world_w() || b >= bg_world_h()) return 0;
    for (s32 py = t & ~7; py <= b; py += 8)
        for (s32 px = l & ~7; px <= r; px += 8)
            if (physics_tile_at(px, py) & TILE_SOLID) return 0;
    return 1;
}

void bhv_free_spot_init(Actor *a, const void *params) {
    const Params_free_spot *p = params;
    s32 x = fx_to_int(a->x), y = fx_to_int(a->y);
    if (clear_at(a, x, y)) return;
    // The nearest clear grid spot, ring by ring (each ring STEP farther out on its longest
    // side). A ring's closest spots are as far as its size, so once a spot nearer than that
    // is found, no farther ring can beat it.
    s32 best = -1, bx = 0, by = 0;
    for (s32 k = STEP; k <= p->reach; k += STEP) {
        if (best >= 0 && best <= k * k) break;
        for (s32 i = -k; i <= k; i += STEP) {
            const s32 cand[4][2] = { { i, -k }, { i, k }, { -k, i }, { k, i } };
            for (int c = 0; c < 4; c++) {
                s32 dx = cand[c][0], dy = cand[c][1], d = dx * dx + dy * dy;
                if ((best < 0 || d < best) && clear_at(a, x + dx, y + dy)) {
                    best = d;
                    bx = dx;
                    by = dy;
                }
            }
        }
    }
    if (best < 0) return;                       // nothing clear in reach: it stays
    // Then back toward where it appeared, 1 px at a time on each axis while still clear, so it
    // lands right at the edge of the free ground (in a corner, at the corner nearest to it).
    for (int moved = 1; moved; ) {
        moved = 0;
        if (bx && clear_at(a, x + bx - (bx > 0 ? 1 : -1), y + by)) { bx += bx > 0 ? -1 : 1; moved = 1; }
        if (by && clear_at(a, x + bx, y + by - (by > 0 ? 1 : -1))) { by += by > 0 ? -1 : 1; moved = 1; }
    }
    x += bx;
    y += by;
    a->x = a->prev_x = fx_from_int(x);
    a->y = a->prev_y = fx_from_int(y);
    a->home_x = x;
    a->home_y = y;
}
