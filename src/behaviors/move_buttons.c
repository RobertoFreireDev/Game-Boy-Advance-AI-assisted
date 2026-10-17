// move_buttons - the player's moves on buttons. Each of the monster's 3 moves recharges on its
// own timer once it is unlocked (by level; charge_var can make every timer shorter). A fires
// move 1, B fires move 2 and R fires move 3, each on a single press. A ready move always goes off and uses its charge, even with nothing in
// reach (aiming is the player's job). The HUD gauges show each charge through game variables.
// A button whose move is still locked buzzes, and so does any move button while the monster is
// paralyzed or frozen; a move that went off at nothing plays the miss sound (the hit sounds come
// from the move).
#include "move_buttons.h"
#include "monster.h"
#include "moves.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/vars.h"
#include "engine/actions.h"
#include "engine/audio.h"

enum { ST_C1, ST_C2, ST_C3, ST_MISC };     // misc: unlocked | blink timers << 8, 16, 24

#define BLINK_TICKS 16

// Gauge values: 0 locked, 1 empty .. 5 full, 6 = the flash when it becomes full.
static s32 gauge(u32 charge, u32 full, int unlocked, int blink) {
    if (!unlocked) return 0;
    if (charge >= full) return blink ? 6 : 5;
    // 1 + charge * 4 / full, without a divide (a slow library call in Thumb code, 3 a frame):
    // charge < full here, so the quotient is 0-3.
    u32 c4 = charge * 4;
    return 1 + (c4 >= full) + (c4 >= full * 2) + (c4 >= full * 3);
}

// Ticks a move takes to recharge: its charge, cut by charge_var percent (at most 90 %), then
// by the monster's home bonus in its home world.
static u32 full_charge(const Actor *a, const Params_move_buttons *p, const MoveData *m) {
    if (!m) return 1;
    u32 full = m->charge;
    s32 cut = p->charge_var >= 0 ? vars_get(p->charge_var) : 0;
    if (cut > 90) cut = 90;
    if (cut > 0) full = fx_udiv100(full * (u32)(100 - cut));
    full = monster_recharge(a, full);
    return full ? full : 1;
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
    st[ST_C1] = st[ST_C2] = st[ST_C3] = 0;
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
        u32 full = full_charge(a, p, m);
        u32 blink = (st[ST_MISC] >> shift[i]) & 0xFF;
        if (blink) blink--;
        if (i < unlocked && m && st[ST_C1 + i] < full) {
            if (++st[ST_C1 + i] >= full) blink = BLINK_TICKS;
        }
        misc |= blink << shift[i];
        if (gauge_var[i] >= 0)
            vars_set(gauge_var[i], gauge(st[ST_C1 + i], full, i < unlocked, (blink & 4) != 0));
    }
    st[ST_MISC] = misc;

    int can = monster_can_act(a);
    const u16 button[3] = { p->button_1, p->button_2, p->button_3 };
    for (int i = 0; i < 3; i++) {                       // A = move 1, B = move 2, R = move 3
        const MoveData *m = sp->moves[i];
        if (!input_pressed(button[i])) continue;
        if (!can) { if (monster_held(a)) sound(p->locked_sfx); continue; }   // paralyzed: it can't
        if (i >= unlocked || !m) sound(p->locked_sfx);
        else if (st[ST_C1 + i] >= full_charge(a, p, m) && fire(a, p, m)) st[ST_C1 + i] = 0;
    }
}
