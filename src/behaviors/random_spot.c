// random_spot - when it appears, the actor moves to a spot picked at random: where it was
// placed or one of its other spots (scene pixels), each as likely.
#include "random_spot.h"
#include "behavior_params.h"

void bhv_random_spot_init(Actor *a, const void *params) {
    const Params_random_spot *p = params;
    int k = rng_range(0, p->spots.count);       // count = stay where it was placed
    if (k >= p->spots.count) return;
    s32 x = p->spots.xy[2 * k], y = p->spots.xy[2 * k + 1];
    a->x = a->prev_x = fx_from_int(x);
    a->y = a->prev_y = fx_from_int(y);
    a->home_x = x;
    a->home_y = y;
}
