// species_select - shows the big portrait of the species chosen in a variable, bobbing gently.
// With `browse`, Left / Right pick another species (wrapping around): the portrait slides out,
// the new one slides in and plays its cry. The confirm button runs the pick actions once.
#include "species_select.h"
#include "behavior_params.h"
#include <tonc_memdef.h>
#include "engine/input.h"
#include "engine/vars.h"
#include "engine/audio.h"
#include "engine/core.h"
#include "engine/actions.h"
#include "engine/sprites.h"

enum { ST_SHOWN, ST_PHASE, ST_TIMER, ST_DIR, ST_PICKED };
enum { PH_IDLE, PH_OUT, PH_IN };

static const SpeciesData *species_at(s32 i) {
    const BattleData *b = g_game.battle;
    if (!b || !b->species_count) return NULL;
    if (i < 0 || i >= b->species_count) i = 0;
    return b->species[i];
}

static void cry(s32 i) {
    const SpeciesData *sp = species_at(i);
    if (sp && sp->cry) audio_play_sfx(sp->cry);
}

void bhv_species_select_init(Actor *a, const void *params) {
    const Params_species_select *p = params;
    u32 *st = bhv_state(a);
    st[ST_SHOWN] = (u32)vars_get(p->var);
    st[ST_PHASE] = PH_IDLE;
    st[ST_TIMER] = 0;
    st[ST_DIR] = 0;
    st[ST_PICKED] = 0;
    if (p->cry_on_start) cry(vars_get(p->var));
}

void bhv_species_select_update(Actor *a, const void *params) {
    const Params_species_select *p = params;
    u32 *st = bhv_state(a);
    const BattleData *b = g_game.battle;
    if (!b || !b->species_count) return;
    int n = b->species_count;
    s32 off = 0;
    int ticks = p->slide_ticks > 0 ? p->slide_ticks : 1;
    s32 side = st[ST_DIR] ? -1 : 1;                 // dir 1 = the next one comes from the right
    if (st[ST_PHASE] == PH_OUT) {
        off = -side * p->slide * (s32)st[ST_TIMER] / ticks;
        if (++st[ST_TIMER] > (u32)ticks) {
            st[ST_SHOWN] = (u32)vars_get(p->var);
            st[ST_PHASE] = PH_IN;
            st[ST_TIMER] = 0;
            cry((s32)st[ST_SHOWN]);
        }
    } else if (st[ST_PHASE] == PH_IN) {
        off = side * p->slide * (ticks - (s32)st[ST_TIMER]) / ticks;
        if (++st[ST_TIMER] > (u32)ticks) st[ST_PHASE] = PH_IDLE;
    } else if (!st[ST_PICKED]) {
        int dir = input_pressed(KEY_RIGHT) - input_pressed(KEY_LEFT);
        if (dir && p->browse) {
            vars_set(p->var, (vars_get(p->var) + dir + n) % n);
            st[ST_DIR] = dir > 0;
            st[ST_PHASE] = PH_OUT;
            st[ST_TIMER] = 0;
            if (p->move_sound) audio_play_sfx(p->move_sound);
        } else if (input_pressed(p->button)) {
            st[ST_PICKED] = 1;
            scripts_start(p->on_pick, a);
        }
    }
    const SpeciesData *sp = species_at((s32)st[ST_SHOWN]);
    if (!sp || !sp->portrait) return;
    s32 bob = (fx_sin_deg((s32)(core_frame() * 6 % 360)) * p->bob) >> FX_SHIFT;
    sprites_queue(sp->portrait, 0, a, off - sp->portrait->width / 2, bob - sp->portrait->height / 2, 0, NULL);
}
