// monster - species, level, hit points, statuses, damage, XP and fainting (see monster.h).
#include <string.h>
#include "monster.h"
#include "moves.h"
#include "monster_ai.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"
#include "engine/audio.h"
#include "engine/core.h"
#include "engine/physics.h"
#include "engine/sprites.h"
#include "engine/particles.h"
#include "engine/floattext.h"

#define FLASH_TICKS   4         // white flash when hit
#define PUSH_PX       2         // knockback when hit
#define STATUS_EVERY  60        // poison hurts once a second
#define FAINT_TICKS   30        // a wild monster flashes and blinks out
#define PLAYER_FAINT  120

typedef struct {
    const SpeciesData *species;
    s16 hp, hp_max;             // wild monsters (the player's live in game variables)
    u8 level, status, team, buff;
    u16 status_ticks, status_clock;
    u16 tox_step, buff_ticks;
    u16 power;
    u16 xp_known;               // player: 1 once the XP bar was worked out (for xp_seen)
    s32 xp_seen;
} Monster;

EWRAM_BSS static Monster s_mon[MAX_ACTORS];

static const Params_monster *params_of(const Actor *a) {
    if (!a) return NULL;
    int i = actor_logic_index(a, BHV_MONSTER);
    return i < 0 ? NULL : (const Params_monster *)a->logic[i].params;
}

static Monster *mon(const Actor *a) {
    return (a && a->active && actor_logic_index(a, BHV_MONSTER) >= 0) ? &s_mon[actor_index(a)] : NULL;
}

const SpeciesData *monster_species(const Actor *a) {
    const Monster *m = mon(a);
    return m ? m->species : NULL;
}

int monster_team(const Actor *a) {
    const Monster *m = mon(a);
    return m ? m->team : TEAM_WILD;
}

int monster_level(const Actor *a) {
    const Monster *m = mon(a);
    return m ? m->level : 1;
}

int monster_power(const Actor *a) {
    const Monster *m = mon(a);
    return m ? m->power : 100;
}

int monster_can_act(const Actor *a) {
    const Monster *m = mon(a);
    return m && actor_alive(a) && m->status != STATUS_PAR && m->status != STATUS_FREEZE;
}

int monster_targetable(const Actor *a) {
    const Monster *m = mon(a);
    return m && actor_alive(a) && m->buff != BUFF_FLY && !monster_ai_asleep(a);
}

int monster_is_foe(const Actor *a, const Actor *b) {
    return a != b && monster_targetable(b) && monster_team(a) != monster_team(b);
}

// ---- floating words ------------------------------------------------------------------
// A word over a monster; `row` 0 sits just above its head, each row 9 px higher.
static void say(const Actor *t, int bt, const char *text, int row) {
    const BattleData *b = g_game.battle;
    if (!text) text = b->texts[bt].text;
    ftext_show(fx_to_int(t->x), fx_to_int(t->y) - 17 - 9 * row, text, b->font, b->texts[bt].palette);
}

static void say_number(const Actor *t, int bt, s32 n) {
    char buf[8];
    int k = 0;
    char digits[8];
    do { digits[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 6);
    for (int i = 0; i < k; i++) buf[i] = digits[k - 1 - i];
    buf[k] = 0;
    say(t, bt, buf, 0);
}

static void battle_sound(int bs) {
    const SfxData *s = g_game.battle->sounds[bs];
    if (s) audio_play_sfx(s);
}

// ---- the player's level and XP -----------------------------------------------------------
static Actor *player_monster(void) {
    Actor *p = actor_player();
    return mon(p) ? p : NULL;
}

// Total XP needed to reach level lv + 1 (0 for level 0).
static s32 xp_needed(const Params_monster *p, s32 lv) {
    return lv <= 0 ? 0 : p->xp_first + p->xp_step * (lv - 1);
}

static void give_xp(s32 amount) {
    Actor *pl = player_monster();
    const Params_monster *p = pl ? params_of(pl) : NULL;
    if (p && p->xp_var >= 0 && amount > 0) vars_add(p->xp_var, amount);
}

// Level ups (as many as the XP pays for) and the XP bar's 0-100 progress.
static void player_progress(Actor *a, const Params_monster *p, Monster *m) {
    s32 xp = vars_get(p->xp_var);
    s32 lv = vars_get(p->level_var);
    if (lv < 1) lv = 1;
    while (lv < p->max_level && xp >= xp_needed(p, lv)) {
        lv++;
        vars_set(p->level_var, lv);
        vars_add(p->hp_max_var, p->hp_per_level);   // not a full heal: only the new points
        vars_add(p->hp_var, p->hp_per_level);
        scripts_start(p->on_level_up, a);
    }
    m->level = (u8)lv;
    if (xp == m->xp_seen && m->xp_known) return;
    m->xp_seen = xp;
    m->xp_known = 1;
    s32 pct = 100;
    if (lv < p->max_level) {
        s32 lo = xp_needed(p, lv - 1), hi = xp_needed(p, lv);
        pct = hi > lo ? (xp - lo) * 100 / (hi - lo) : 100;
    }
    if (p->xp_pct_var >= 0) vars_set(p->xp_pct_var, pct < 0 ? 0 : pct > 100 ? 100 : pct);
}

// ---- hit points, fainting ----------------------------------------------------------------
static s32 hp_of(const Params_monster *p, const Monster *m) {
    return p->hp_var >= 0 ? vars_get(p->hp_var) : m->hp;
}

static void faint(Actor *a, const Params_monster *p, Monster *m) {
    m->status = 0;
    m->buff = 0;
    if (m->team == TEAM_WILD) {
        give_xp(p->xp_reward);
        if (p->kills_var >= 0) vars_add(p->kills_var, 1);
    }
    battle_sound(BS_FAINT);
    scripts_start(p->on_death, a);
    actor_die(a, 1);
    a->flags &= (u8)~ACTOR_HIDDEN;
    a->die_timer = m->team == TEAM_PLAYER ? PLAYER_FAINT : FAINT_TICKS;
    a->blink = a->die_timer;            // blinks out
    a->flash = FLASH_TICKS;
}

static void hurt(Actor *a, s32 amount) {
    const Params_monster *p = params_of(a);
    Monster *m = mon(a);
    if (!p || !m || amount <= 0 || !actor_alive(a)) return;
    s32 hp = hp_of(p, m) - amount;
    if (hp < 0) hp = 0;
    if (p->hp_var >= 0) vars_set(p->hp_var, hp);
    else m->hp = (s16)hp;
    const Params_monster *pp = m->team == TEAM_WILD ? params_of(player_monster()) : NULL;
    if (pp) give_xp(pp->xp_per_hit);
    if (hp == 0) faint(a, p, m);
}

// Push a monster `px` pixels away from (fx, fy), unless a wall is in the way.
static void push(Actor *a, s32 fx, s32 fy, int px) {
    s32 dx = fx_to_int(a->x) - fx, dy = fx_to_int(a->y) - fy;
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    int sx = ax * 2 >= ay ? fx_sign(dx) : 0, sy = ay * 2 >= ax ? fx_sign(dy) : 0;
    s32 nx = fx_to_int(a->x) + sx * px, ny = fx_to_int(a->y) + sy * px;
    s32 l = nx + a->hb_x, t = ny + a->hb_y, r = l + a->hb_w - 1, b = t + a->hb_h - 1;
    if ((physics_tile_at(l, t) | physics_tile_at(r, t) | physics_tile_at(l, b) | physics_tile_at(r, b)) & TILE_SOLID)
        return;
    a->x = fx_from_int(nx);
    a->y = fx_from_int(ny);
}

static void set_status(Actor *t, Monster *m, int status, int ticks) {
    m->status = (u8)status;
    m->status_ticks = (u16)ticks;
    m->status_clock = 0;
    m->tox_step = 0;
    if (status == STATUS_PAR) say(t, BT_PAR, NULL, 1);
    else if (status == STATUS_PSN) say(t, BT_PSN, NULL, 1);
    else if (status == STATUS_TOX) say(t, BT_TOX, NULL, 1);
}

void monster_hit(Actor *t, const MonsterHit *h) {
    Monster *m = mon(t);
    const BattleData *b = g_game.battle;
    const MoveData *mv = h->move;
    if (!m || !b || !actor_alive(t)) return;
    if (mv->power > 0) {
        if (h->roll && mv->accuracy < 100 && rng_range(1, 100) > mv->accuracy) {
            say(t, BT_MISS, NULL, 0);
            battle_sound(BS_MISS);
            return;
        }
        // damage = power x (1 + 0.15 x (level - 1)) x type x user power, rounded, at least 1
        int pct = b->chart[mv->type * b->type_count + m->species->type];
        s32 d = (s32)mv->power * (100 + 15 * (h->level - 1));          // x 100
        d = d * pct / 100 * h->power / 100;
        d = (d + 50) / 100;
        if (d < 1) d = 1;
        say_number(t, BT_DAMAGE, d);
        if (pct > 100) { say(t, BT_SUPER, NULL, 1); battle_sound(BS_SUPER); }
        else if (pct < 100) { say(t, BT_WEAK, NULL, 1); battle_sound(BS_WEAK); }
        else battle_sound(BS_HIT);
        t->flash_pal = b->flash_palette;
        t->flash = FLASH_TICKS;
        push(t, h->from_x, h->from_y, PUSH_PX);
        hurt(t, d);
        if (!actor_alive(t)) return;
    }
    if (mv->status != STATUS_NONE && rng_range(1, 100) <= mv->status_chance)
        set_status(t, m, mv->status, rng_range(mv->status_min, mv->status_max));
}

// ---- buffs (Fly, Phase) ----------------------------------------------------------------
void monster_buff(Actor *a, int buff, int ticks) {
    Monster *m = mon(a);
    if (!m || buff == BUFF_NONE) return;
    m->buff = (u8)buff;
    m->buff_ticks = (u16)ticks;
    a->solid_mode = SOLID_NONE;                         // walks through monsters
    a->collides &= (u16)~(1u << CAT_ENEMY);
    if (buff == BUFF_FLY) a->flags |= ACTOR_HIDDEN;     // only its shadow shows
}

static int overlaps_foe(const Actor *a) {
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor *o = &g_actors[i];
        if (o != a && actor_alive(o) && o->solid_mode != SOLID_NONE && physics_overlap(a, o)) return 1;
    }
    return 0;
}

static void buff_update(Actor *a, const Params_monster *p, Monster *m) {
    if (m->buff == BUFF_FLY && p->shadow)
        sprites_queue(p->shadow, 0, a, -p->shadow->width / 2, -p->shadow->height + 1, 0, NULL);
    if (m->buff == BUFF_PHASE) a->blink = (core_frame() & 8) ? 4 : 0;     // ghostly flicker
    if (m->buff_ticks > 1) { m->buff_ticks--; return; }
    if (overlaps_foe(a)) return;                        // land beside the monsters, not inside one
    m->buff = 0;
    m->buff_ticks = 0;
    a->blink = 0;
    a->flags &= (u8)~ACTOR_HIDDEN;
    a->solid_mode = a->body && a->body->solid ? SOLID_FULL : SOLID_NONE;
    a->collides = a->body ? a->body->collides : 0;
}

// ---- the behavior --------------------------------------------------------------------
// Which species this monster is: fixed, from a variable, or random (never the avoided one).
static const SpeciesData *pick_species(const Params_monster *p) {
    const BattleData *b = g_game.battle;
    int n = b->species_count;
    s32 avoid = p->avoid_var >= 0 ? vars_get(p->avoid_var) : -1;
    if (p->species_var >= 0) {
        s32 i = vars_get(p->species_var);
        return i >= 0 && i < n ? b->species[i] : b->species[0];
    }
    if (p->random) {
        int k = rng_range(0, (avoid >= 0 && avoid < n ? n - 2 : n - 1));
        if (avoid >= 0 && k >= avoid) k++;
        return b->species[k];
    }
    if (avoid >= 0 && avoid < n && b->species[avoid] == p->species) return NULL;   // the player's kind
    return p->species;
}

void bhv_monster_init(Actor *a, const void *params) {
    const Params_monster *p = params;
    Monster *m = &s_mon[actor_index(a)];
    memset(m, 0, sizeof(*m));
    const SpeciesData *sp = g_game.battle ? pick_species(p) : NULL;
    if (!sp) {                          // not in this run
        actor_destroy(a);
        return;
    }
    m->species = sp;
    m->team = a->type == NT_PLAYER ? TEAM_PLAYER : TEAM_WILD;
    m->level = (u8)(p->level_var >= 0 ? vars_get(p->level_var) : p->level);
    m->hp = m->hp_max = (s16)p->hp;
    m->power = (u16)p->power;
    actor_set_body(a, sp->body);
    if (p->alpha) a->pal = sp->alpha_palette;
    a->flash_pal = g_game.battle->flash_palette;
}

void bhv_monster_update(Actor *a, const void *params) {
    const Params_monster *p = params;
    Monster *m = mon(a);
    if (!m) return;
    moves_tick();                       // ground zones (once per frame, whoever asks first)
    if (p->level_var >= 0) {
        if (p->xp_var >= 0) player_progress(a, p, m);
        else m->level = (u8)vars_get(p->level_var);
    }
    if (m->status != STATUS_NONE) {
        if (m->status == STATUS_PSN || m->status == STATUS_TOX) {
            if (++m->status_clock >= STATUS_EVERY) {    // poison: 1 a second; toxic: 1, 2, 3...
                m->status_clock = 0;
                s32 d = m->status == STATUS_PSN ? 1 : ++m->tox_step;
                say_number(a, m->status == STATUS_PSN ? BT_PSN : BT_TOX, d);
                hurt(a, d);
                if (!actor_alive(a)) return;
            }
        }
        if (m->status == STATUS_PAR && (core_frame() & 7) == 0) {     // flickers yellow
            a->flash_pal = g_game.battle->par_palette;
            a->flash = FLASH_TICKS;
        } else if (a->flash == 0) {
            a->flash_pal = g_game.battle->flash_palette;
        }
        if (m->status_ticks && --m->status_ticks == 0) m->status = STATUS_NONE;
    }
    if (m->buff) buff_update(a, p, m);
    if (p->crown && a->body) {
        int bob = (core_frame() >> 4) & 1;
        sprites_queue(p->crown, (u16)bob, a, -p->crown->width / 2,
                      -a->body->origin_y - p->crown->height + 3 - bob, 0, NULL);
    }
    if (p->aura && (core_frame() % 24) == (u32)(actor_index(a) % 24))
        particles_burst(p->aura, fx_to_int(a->x) + rng_range(-8, 8), fx_to_int(a->y) - rng_range(2, 14));
}
