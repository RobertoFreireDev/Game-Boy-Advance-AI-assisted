// day_clock - the in-game clock (one game minute every ticks_per_minute ticks). Keeps minute,
// hour (0-23) and day variables, runs on_hour at every new hour (and once when the scene
// starts, so music and night darkness match the hour) and on_new_day when the clock reaches
// new_day_hour. The clock stops while a menu or dialog pauses the game.
#include "day_clock.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"

enum { ST_TICKS, ST_STARTED };

void bhv_day_clock_update(Actor *a, const void *params) {
    const Params_day_clock *p = params;
    u32 *st = bhv_state(a);
    if (!st[ST_STARTED]) {                      // first tick, after the scene set its music
        st[ST_STARTED] = 1;
        if (p->hour_at_start) scripts_start(p->on_hour, a);
    }
    if (++st[ST_TICKS] < (u32)p->ticks_per_minute) return;
    st[ST_TICKS] = 0;
    vars_add(p->minute_var, 1);
    if (vars_get(p->minute_var) < 60) return;
    vars_set(p->minute_var, 0);
    s32 h = vars_get(p->hour_var) + 1;
    if (h >= 24) h = 0;
    vars_set(p->hour_var, h);
    if (h == p->new_day_hour) {
        vars_add(p->day_var, 1);
        scripts_start(p->on_new_day, a);
    }
    scripts_start(p->on_hour, a);
}
