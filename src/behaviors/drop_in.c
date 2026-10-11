// drop_in - falls in from above and bounces before settling (a title logo). It moves itself,
// or a whole background layer: layer 1-3 drops that tilemap instead (the actor stays hidden).
#include "drop_in.h"
#include "behavior_params.h"
#include "engine/audio.h"
#include "engine/bg.h"

enum { ST_OFFSET, ST_SPEED, ST_BOUNCES, ST_DELAY };

static void place(Actor *a, const Params_drop_in *p, s32 off) {
    if (p->layer >= 1 && p->layer <= 3) bg_set_offset(p->layer, 0, off);
    else a->y = fx_from_int(a->home_y + off);
}

void bhv_drop_in_init(Actor *a, const void *params) {
    const Params_drop_in *p = params;
    u32 *st = bhv_state(a);
    st[ST_OFFSET] = (u32)fx_from_int(-p->height);
    st[ST_SPEED] = 0;
    st[ST_BOUNCES] = 0;
    st[ST_DELAY] = (u32)p->delay;
    place(a, p, -p->height);
}

void bhv_drop_in_update(Actor *a, const void *params) {
    const Params_drop_in *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_DELAY]) { st[ST_DELAY]--; return; }
    fixed off = (fixed)st[ST_OFFSET], v = (fixed)st[ST_SPEED];
    if (off == 0 && v == 0) return;                 // settled
    v += p->gravity;
    off += v;
    if (off >= 0) {                                 // hit the ground: bounce, or settle
        off = 0;
        if ((s32)st[ST_BOUNCES] < p->bounces) {
            st[ST_BOUNCES]++;
            v = -v * p->bounce / FX_ONE;
            if (p->sound) audio_play_sfx(p->sound);
        } else {
            v = 0;
        }
    }
    st[ST_OFFSET] = (u32)off;
    st[ST_SPEED] = (u32)v;
    place(a, p, fx_to_int(off));
}
