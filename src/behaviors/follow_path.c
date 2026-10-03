// follow_path - travel along points (relative to the start), pausing at each one.
// The speed is set so physics lands exactly on the path; riders are carried along.
#include "follow_path.h"
#include "behavior_params.h"

enum { ST_INDEX, ST_PROGRESS, ST_WAIT, ST_DIR, ST_DONE };

void bhv_follow_path_init(Actor *a, const void *params) {
    const Params_follow_path *p = params;
    u32 *st = bhv_state(a);
    st[ST_INDEX] = 0;
    st[ST_PROGRESS] = 0;
    st[ST_WAIT] = (u32)p->wait;
    st[ST_DIR] = 1;
    st[ST_DONE] = 0;
    a->gravity = 0;
}

void bhv_follow_path_update(Actor *a, const void *params) {
    const Params_follow_path *p = params;
    u32 *st = bhv_state(a);
    int count = p->points.count;
    a->vx = a->vy = 0;
    if (count < 2 || st[ST_DONE]) return;
    if (st[ST_WAIT]) { st[ST_WAIT]--; return; }

    int i = (int)st[ST_INDEX];
    int dir = (s32)st[ST_DIR];
    int j = p->mode == FOLLOW_PATH_MODE_LOOP ? (i + 1) % count : i + dir;
    const s16 *xy = p->points.xy;
    s32 sx = a->home_x + xy[2 * i], sy = a->home_y + xy[2 * i + 1];
    s32 ex = a->home_x + xy[2 * j], ey = a->home_y + xy[2 * j + 1];
    s32 dx = ex - sx, dy = ey - sy;
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
    fixed prog = (fixed)st[ST_PROGRESS] + p->speed;
    fixed tx, ty;
    if (len == 0 || prog >= fx_from_int(len)) {
        tx = fx_from_int(ex);
        ty = fx_from_int(ey);
        st[ST_INDEX] = (u32)j;
        st[ST_PROGRESS] = 0;
        st[ST_WAIT] = (u32)p->wait;
        if (p->mode == FOLLOW_PATH_MODE_PING_PONG) {
            if ((j == count - 1 && dir > 0) || (j == 0 && dir < 0)) st[ST_DIR] = (u32)-dir;
        } else if (p->mode == FOLLOW_PATH_MODE_ONCE && j == count - 1) {
            st[ST_DONE] = 1;
        }
    } else {
        tx = fx_from_int(sx) + dx * prog / len;
        ty = fx_from_int(sy) + dy * prog / len;
        st[ST_PROGRESS] = (u32)prog;
    }
    a->vx = tx - a->x;
    a->vy = ty - a->y;
    if (a->vx) a->facing_left = a->vx < 0;
}
