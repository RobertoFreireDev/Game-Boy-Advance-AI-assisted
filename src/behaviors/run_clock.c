// run_clock - counts play time: adds 1 to a seconds variable every 60 ticks and keeps
// minutes / seconds variables for a mm:ss display. It stops while the game is paused
// (menus pause every behavior).
#include "run_clock.h"
#include "behavior_params.h"
#include "engine/vars.h"

enum { ST_TICKS };

void bhv_run_clock_update(Actor *a, const void *params) {
    const Params_run_clock *p = params;
    u32 *st = bhv_state(a);
    if (++st[ST_TICKS] < 60) return;
    st[ST_TICKS] = 0;
    vars_add(p->total_var, 1);
    s32 t = vars_get(p->total_var);
    if (p->minutes_var >= 0) vars_set(p->minutes_var, t / 60);
    if (p->seconds_var >= 0) vars_set(p->seconds_var, t % 60);
}
