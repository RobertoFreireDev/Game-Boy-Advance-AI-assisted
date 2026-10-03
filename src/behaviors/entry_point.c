// entry_point - an invisible marker: when the scene starts and a game variable (the door the
// player came through) holds its value, the player is moved onto it. Optionally the position
// comes from two variables instead (back to where the player stood).
#include "entry_point.h"
#include "behavior_params.h"
#include "engine/vars.h"

enum { ST_STATE };          // 0 = not this entry, 1 = waiting for the player, 2 = done

static int place(Actor *a, const Params_entry_point *p) {
    Actor *pl = actor_player();
    if (!pl) return 0;
    s32 x = p->x_var >= 0 ? vars_get(p->x_var) : fx_to_int(a->x);
    s32 y = p->y_var >= 0 ? vars_get(p->y_var) : fx_to_int(a->y);
    pl->x = pl->prev_x = fx_from_int(x);
    pl->y = pl->prev_y = fx_from_int(y);
    pl->vx = pl->vy = 0;
    pl->facing_left = p->face_left;
    pl->facing = p->face_left ? DIR_LEFT : DIR_RIGHT;
    return 1;
}

void bhv_entry_point_init(Actor *a, const void *params) {
    const Params_entry_point *p = params;
    u32 *st = bhv_state(a);
    st[ST_STATE] = 0;
    if (vars_get(p->var) == p->value) st[ST_STATE] = place(a, p) ? 2 : 1;
}

void bhv_entry_point_update(Actor *a, const void *params) {
    u32 *st = bhv_state(a);
    if (st[ST_STATE] == 1 && place(a, params)) st[ST_STATE] = 2;   // the player was placed after us
}
