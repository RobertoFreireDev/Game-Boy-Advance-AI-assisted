// level_up - experience levels: when the XP variable reaches the amount the next level needs,
// that XP is spent, the level goes up and the actions run (e.g. open the level-up menu).
// Needed XP = first + growth * (level - 1). Also keeps a 0-100 progress variable for a bar.
#include "level_up.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"

static s32 needed(const Params_level_up *p, s32 level) {
    s32 n = p->first + p->growth * (level - 1);
    return n < 1 ? 1 : n;
}

void bhv_level_up_update(Actor *a, const void *params) {
    const Params_level_up *p = params;
    s32 lv = vars_get(p->level_var), xp = vars_get(p->xp_var);
    s32 need = needed(p, lv);
    if (xp >= need) {                   // one level per tick; the menu pauses the game anyway
        xp -= need;
        vars_set(p->xp_var, xp);
        vars_set(p->level_var, ++lv);
        need = needed(p, lv);
        scripts_start(p->on_level_up, a);
    }
    if (p->progress_var >= 0) vars_set(p->progress_var, xp >= need ? 100 : xp * 100 / need);
}
