// place_from_vars - when it appears, the object moves to the position held in two game
// variables (for example where the player last stood before dying).
#include "place_from_vars.h"
#include "behavior_params.h"
#include "engine/vars.h"

void bhv_place_from_vars_init(Actor *a, const void *params) {
    const Params_place_from_vars *p = params;
    a->home_x = vars_get(p->x_var) + p->offset_x;
    a->home_y = vars_get(p->y_var) + p->offset_y;
    a->x = a->prev_x = fx_from_int(a->home_x);
    a->y = a->prev_y = fx_from_int(a->home_y);
}
