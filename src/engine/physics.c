// physics.c - physics questions behaviors ask now and then (not every frame), kept out of
// IWRAM: can an actor stand at a spot? The per-frame work is in physics.iwram.c.
#include "physics.h"

int physics_free_at(const Actor *a, fixed x, fixed y) {
    s32 l = fx_to_int(x) + a->hb_x, t = fx_to_int(y) + a->hb_y;
    s32 r = l + a->hb_w - 1, b = t + a->hb_h - 1;
    u32 want = a->collides;
    if (want & (1u << CAT_TILES))
        for (s32 py = t & ~7; py <= b; py += 8)
            for (s32 px = l & ~7; px <= r; px += 8)
                if (physics_tile_at(px, py) & TILE_SOLID) return 0;
    for (const Actor *s = g_actors; s < g_actors + MAX_ACTORS; s++) {
        if (!s->active || s->solid_mode != SOLID_FULL || s == a || !(want & (1u << s->category))) continue;
        s32 sl = fx_to_int(s->x) + s->hb_x, st = fx_to_int(s->y) + s->hb_y;
        if (l < sl + s->hb_w && sl <= r && t < st + s->hb_h && st <= b) return 0;
    }
    return 1;
}
