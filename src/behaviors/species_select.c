// species_select - shows the big portrait of the species chosen in a variable, bobbing gently.
// With `browse`, Left / Right pick another species (wrapping around): the portrait slides out,
// the new one slides in and plays its cry. The confirm button runs the pick actions once.
// Species past `unlocked_var` are locked: drawn as a silhouette, silent, and they can't be picked.
// With `passes` 2 or more the list of species repeats (a second game mode with the same monsters):
// the entry shown is pass x species count + species, the pass is kept in `pass_var`, and the passes
// after the first are locked as a whole until `pass_unlock_var` reaches `pass_unlock_at`.
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

static int species_n(void) {
    const BattleData *b = g_game.battle;
    return b ? b->species_count : 0;
}

static int passes(const Params_species_select *p) {
    return p->pass_var >= 0 && p->passes > 1 ? p->passes : 1;
}

// The entry the variables point at: pass x species count + species number.
static s32 entry_now(const Params_species_select *p) {
    int n = species_n();
    s32 i = vars_get(p->var), k = p->pass_var >= 0 ? vars_get(p->pass_var) : 0;
    if (i < 0 || i >= n) i = 0;
    if (k < 0 || k >= passes(p)) k = 0;
    return k * n + i;
}

static const SpeciesData *species_at(s32 entry) {
    int n = species_n();
    return n ? g_game.battle->species[entry % n] : NULL;
}

// 0 = the entry can be picked; else 1 + its pass (1 = a species of the first pass not unlocked yet,
// 2 = the second pass while it is still locked as a whole...).
static int locked(const Params_species_select *p, s32 entry) {
    int n = species_n();
    int pass = (int)(entry / n);
    if (pass == 0) return p->unlocked_var >= 0 && entry >= vars_get(p->unlocked_var);
    if (p->pass_unlock_var >= 0 && vars_get(p->pass_unlock_var) < p->pass_unlock_at) return 1 + pass;
    return 0;
}

static void cry(const Params_species_select *p, s32 entry) {
    const SpeciesData *sp = species_at(entry);
    if (sp && sp->cry && !locked(p, entry)) audio_play_sfx(sp->cry);
}

void bhv_species_select_init(Actor *a, const void *params) {
    const Params_species_select *p = params;
    u32 *st = bhv_state(a);
    st[ST_SHOWN] = species_n() ? (u32)entry_now(p) : 0;
    st[ST_PHASE] = PH_IDLE;
    st[ST_TIMER] = 0;
    st[ST_DIR] = 0;
    st[ST_PICKED] = 0;
    if (p->cry_on_start && species_n()) cry(p, (s32)st[ST_SHOWN]);
}

void bhv_species_select_update(Actor *a, const void *params) {
    const Params_species_select *p = params;
    u32 *st = bhv_state(a);
    int n = species_n();
    if (!n) return;
    int total = n * passes(p);
    s32 off = 0;
    int ticks = p->slide_ticks > 0 ? p->slide_ticks : 1;
    s32 side = st[ST_DIR] ? -1 : 1;                 // dir 1 = the next one comes from the right
    if (st[ST_PHASE] == PH_OUT) {
        off = -side * p->slide * (s32)st[ST_TIMER] / ticks;
        if (++st[ST_TIMER] > (u32)ticks) {
            st[ST_SHOWN] = (u32)entry_now(p);
            st[ST_PHASE] = PH_IN;
            st[ST_TIMER] = 0;
            cry(p, (s32)st[ST_SHOWN]);
        }
    } else if (st[ST_PHASE] == PH_IN) {
        off = side * p->slide * (ticks - (s32)st[ST_TIMER]) / ticks;
        if (++st[ST_TIMER] > (u32)ticks) st[ST_PHASE] = PH_IDLE;
    } else if (st[ST_SHOWN] != (u32)entry_now(p)) {
        st[ST_SHOWN] = (u32)entry_now(p);           // changed by something else (progress cleared)
        cry(p, (s32)st[ST_SHOWN]);
    } else if (!st[ST_PICKED]) {
        int dir = input_pressed(KEY_RIGHT) - input_pressed(KEY_LEFT);
        if (dir && p->browse) {
            s32 e = (entry_now(p) + dir + total) % total;
            vars_set(p->var, e % n);
            if (p->pass_var >= 0) vars_set(p->pass_var, e / n);
            st[ST_DIR] = dir > 0;
            st[ST_PHASE] = PH_OUT;
            st[ST_TIMER] = 0;
            if (p->move_sound) audio_play_sfx(p->move_sound);
        } else if (input_pressed(p->button)) {
            if (locked(p, (s32)st[ST_SHOWN])) {
                if (p->locked_sound) audio_play_sfx(p->locked_sound);
            } else {
                st[ST_PICKED] = 1;
                scripts_start(p->on_pick, a);
            }
        }
    }
    vars_set(p->locked_var, locked(p, entry_now(p)));     // with the name the HUD shows
    vars_set(p->entry_var, entry_now(p));
    const SpeciesData *sp = species_at((s32)st[ST_SHOWN]);
    if (!sp || !sp->portrait) return;
    s32 bob = (fx_sin_deg((s32)(core_frame() * 6 % 360)) * p->bob) >> FX_SHIFT;
    sprites_queue(sp->portrait, 0, a, off - sp->portrait->width / 2, bob - sp->portrait->height / 2, 0,
                  locked(p, (s32)st[ST_SHOWN]) ? p->locked_palette : NULL);
}
