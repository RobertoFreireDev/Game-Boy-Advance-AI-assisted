// move_buttons - the player's moves on buttons. Each of the monster's 3 moves recharges on its
// own timer once it is unlocked (by level). A fires move 1, B fires move 2, and holding R
// without the D-pad shows a ring around the player at once that fills up: when it is full,
// move 3 fires (R is let go before the ring shows again). A ready move always goes off and uses
// its charge, even with nothing in reach (aiming is the player's job). The HUD gauges show each
// charge through game variables. A button whose move is still locked buzzes; a move that went
// off at nothing plays the miss sound (the hit sounds come from the move).
#include "move_buttons.h"
#include "monster.h"
#include "moves.h"
#include "behavior_params.h"
#include <tonc_memdef.h>
#include "engine/input.h"
#include "engine/vars.h"
#include "engine/actions.h"
#include "engine/audio.h"
#include "engine/sprites.h"

enum { ST_C1, ST_C2, ST_C3, ST_HOLD, ST_MISC };     // misc: unlocked | blink timers << 8, 16, 24

#define RING_DOTS   8
#define BLINK_TICKS 16
#define HOLD_SPENT  0xFFFFFFFFu                         // move 3 fired: wait until the button is let go

// Gauge values: 0 locked, 1 empty .. 5 full, 6 = the flash when it becomes full.
static s32 gauge(u32 charge, u32 full, int unlocked, int blink) {
    if (!unlocked) return 0;
    if (charge >= full) return blink ? 6 : 5;
    return 1 + (s32)(charge * 4 / (full ? full : 1));
}

static int unlocked_at(const Params_move_buttons *p, s32 level) {
    return 1 + (level >= p->unlock_2) + (level >= p->unlock_3);
}

static void sound(const SfxData *s) {
    if (s) audio_play_sfx(s);
}

// Fire a ready move: it always goes off; at nothing, the miss sound. Returns 1 if it went off.
static int fire(Actor *a, const Params_move_buttons *p, const MoveData *m) {
    int r = moves_use(a, m, 1);
    if (r == USE_WHIFF) sound(p->miss_sfx);
    return r != USE_NONE;
}

void bhv_move_buttons_init(Actor *a, const void *params) {
    (void)params;
    u32 *st = bhv_state(a);
    st[ST_C1] = st[ST_C2] = st[ST_C3] = st[ST_HOLD] = 0;
    st[ST_MISC] = 0;                    // moves unlocked: worked out on the first tick
}

void bhv_move_buttons_update(Actor *a, const void *params) {
    const Params_move_buttons *p = params;
    u32 *st = bhv_state(a);
    const SpeciesData *sp = monster_species(a);
    if (!sp || !actor_alive(a)) return;
    int unlocked = (int)(st[ST_MISC] & 0xFF);
    int want = unlocked_at(p, vars_get(p->level_var));
    if (unlocked == 0) unlocked = want;                 // first tick (the scene has set the level)
    while (unlocked < want && unlocked < 3) {           // a new move: its timer starts now
        st[ST_C1 + unlocked] = 0;
        unlocked++;
        scripts_start(p->on_unlock, a);
    }
    if (p->unlocked_var >= 0) vars_set(p->unlocked_var, unlocked);

    // Charges and gauges. A move that just became full blinks once on the HUD.
    static const u8 shift[3] = { 8, 16, 24 };
    s16 gauge_var[3] = { p->gauge_1, p->gauge_2, p->gauge_3 };
    u32 misc = (u32)unlocked;
    for (int i = 0; i < 3; i++) {
        const MoveData *m = sp->moves[i];
        u32 blink = (st[ST_MISC] >> shift[i]) & 0xFF;
        if (blink) blink--;
        if (i < unlocked && m && st[ST_C1 + i] < m->charge) {
            if (++st[ST_C1 + i] >= m->charge) blink = BLINK_TICKS;
        }
        misc |= blink << shift[i];
        if (gauge_var[i] >= 0)
            vars_set(gauge_var[i], gauge(st[ST_C1 + i], m ? m->charge : 1, i < unlocked, (blink & 4) != 0));
    }
    st[ST_MISC] = misc;

    int can = monster_can_act(a);
    for (int i = 0; i < 2; i++) {                       // A = move 1, B = move 2
        const MoveData *m = sp->moves[i];
        if (!input_pressed(i == 0 ? p->button_1 : p->button_2) || !can) continue;
        if (i >= unlocked || !m) sound(p->locked_sfx);
        else if (st[ST_C1 + i] >= m->charge && fire(a, p, m)) st[ST_C1 + i] = 0;
    }

    // Hold button 3, D-pad untouched: the ring shows at once and fills; when it is full, move 3
    // fires if it is ready (else the grey ring stays full and nothing fires).
    u32 full = p->hold_ticks ? (u32)p->hold_ticks : 1;
    int dpad = input_held(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if (input_pressed(p->button_3) && can && (unlocked < 3 || !sp->moves[2])) sound(p->locked_sfx);
    if (!input_held(p->button_3)) {
        st[ST_HOLD] = 0;
    } else if (st[ST_HOLD] != HOLD_SPENT) {
        if (dpad || !can) st[ST_HOLD] = 0;
        else if (st[ST_HOLD] < full) st[ST_HOLD]++;
    }
    const MoveData *m3 = sp->moves[2];
    int ready = unlocked >= 3 && m3 && st[ST_C3] >= m3->charge;
    if (st[ST_HOLD] == full && ready) {
        if (fire(a, p, m3)) st[ST_C3] = 0;
        st[ST_HOLD] = HOLD_SPENT;
    }
    if (st[ST_HOLD] && st[ST_HOLD] != HOLD_SPENT && p->ring) {
        int lit = (int)(st[ST_HOLD] * RING_DOTS / full);
        for (int k = 0; k < RING_DOTS; k++) {           // clockwise from the top
            s32 ang = 90 - 45 * k;
            s32 x = (fx_cos_deg(ang) * p->ring_radius) >> FX_SHIFT;
            s32 y = -((fx_sin_deg(ang) * p->ring_radius) >> FX_SHIFT) - 7;
            sprites_queue(p->ring, k < lit ? 1 : 0, a, x - p->ring->width / 2, y - p->ring->height / 2, 0,
                          ready ? NULL : p->ring_off_palette);
        }
    }
}
