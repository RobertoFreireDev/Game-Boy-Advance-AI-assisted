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
#include "engine/camera.h"
#include "engine/ui.h"
#include "engine/physics.h"
#include "engine/sprites.h"
#include "engine/particles.h"
#include "engine/floattext.h"

#define FLASH_TICKS   4         // the paralysis flicker and the faint flash (a hit uses the params)
#define PAR_TREMBLE   1         // px a paralyzed monster trembles sideways (every 2 frames)
#define PUSH_PX       2         // knockback when hit
#define STATUS_EVERY  60        // poison hurts once a second
#define FAINT_TICKS   30        // a wild monster flashes and blinks out
#define PLAYER_FAINT  120

enum { WALK_LEFT, WALK_RIGHT, WALK_UP, WALK_DOWN };

typedef struct {
    const SpeciesData *species;
    s16 hp, hp_max;             // wild monsters (the player's live in game variables)
    u8 level, status, team, buff;
    u8 flashes, flash_gap;      // a hit's flashes still to come, ticks since the last one ended
    u16 status_ticks, status_clock;     // status_clock: poison's second; paralysis' sparks tick
    u16 tox_step, buff_ticks;
    u16 power;
    u16 xp_known;               // player: 1 once the XP bar was worked out (for xp_seen)
    u16 shove_ticks;            // a move's push: ticks of sliding left (it can't move or attack)
    u16 tag_ticks;              // its level tag shows this many more ticks
    u16 away_ticks;             // ticks out of the tag view since it was last in it
    u8 tag_seen;                // 1: in the tag view lately (the tag showed; it won't show again yet)
    u16 walked[4];              // per way (WALK_*): bit k = it walked that way k + 1 ticks ago (walking dodge)
    u32 walk_frame;             // the frame its last step was recorded in
    s32 xp_seen;
    s32 shove_x, shove_y;       // the push's step unit (1/65536 px): the k-th last tick slides k units
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

// A game variable's value, or 0 when the param names none (the player's boosts).
static s32 var_or_0(s16 var) {
    return var >= 0 ? vars_get(var) : 0;
}

// The colors it flashes when a move hits it: its own (wild monsters: red), else the game's (white).
static const PaletteData *hit_palette(const Params_monster *p) {
    return p && p->flash_palette ? p->flash_palette : g_game.battle->flash_palette;
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
    return m ? m->power + var_or_0(params_of(a)->power_var) : 100;
}

int monster_aim(const Actor *a) {
    return mon(a) ? var_or_0(params_of(a)->aim_var) : 0;
}

fixed monster_speed(const Actor *a, fixed speed) {
    const Monster *m = mon(a);
    return m && m->species ? (speed * m->species->speed) >> 8 : speed;
}

int monster_can_act(const Actor *a) {
    const Monster *m = mon(a);
    return m && actor_alive(a) && m->status != STATUS_PAR && m->status != STATUS_FREEZE && !m->shove_ticks;
}

int monster_held(const Actor *a) {
    const Monster *m = mon(a);
    return m && actor_alive(a) && (m->status == STATUS_PAR || m->status == STATUS_FREEZE);
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

// Write n (0 or more, up to 6 digits) at buf, 0-terminated. Returns how many digits.
static int put_number(char *buf, s32 n) {
    int k = 0;
    char digits[8];
    do { digits[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 6);
    for (int i = 0; i < k; i++) buf[i] = digits[k - 1 - i];
    buf[k] = 0;
    return k;
}

static void say_number(const Actor *t, int bt, s32 n) {
    char buf[8];
    put_number(buf, n);
    say(t, bt, buf, 0);
}

// ---- the level tag ----------------------------------------------------------------------
// 1 when its whole body is in the tag view: the screen below the HUD's top panel, shrunk by
// `margin` pixels on every side (so the tag over its head is always on screen).
static int in_tag_view(const Actor *a, int margin) {
    s32 l, t, r, b, cx = camera_x(), cy = camera_y();
    actor_rect(a, &l, &t, &r, &b);
    return l >= cx + margin && r < cx + 240 - margin && t >= cy + ui_hud_top() + margin && b < cy + 160 - margin;
}

// A move reached it: the tag goes away at once (the words of the hit take its place).
static void tag_off(const Actor *a, Monster *m) {
    if (!m->tag_ticks) return;
    m->tag_ticks = 0;
    ftext_unpin(a);
}

// Its level over its head for a while each time it comes into view after being out of it
// long enough (or never seen yet).
static void level_tag(Actor *a, const Params_monster *p, Monster *m) {
    if (in_tag_view(a, p->level_tag_margin)) {
        m->away_ticks = 0;
        if (!m->tag_seen) {
            m->tag_seen = 1;
            m->tag_ticks = (u16)p->level_tag;
        }
    } else if (m->tag_seen && ++m->away_ticks > p->level_tag_reset) {
        m->tag_seen = 0;
    }
    if (!m->tag_ticks) return;
    m->tag_ticks--;
    if ((a->flags & ACTOR_HIDDEN) || !a->body) return;     // flying: only its shadow shows
    const BattleData *bd = g_game.battle;
    char buf[16];
    int n = 0;
    for (const char *c = p->level_tag_text; c && *c && n < 4; c++) buf[n++] = *c;
    put_number(buf + n, m->level);
    s32 dy = -a->body->origin_y - 1;                    // 1 px above the top of its picture
    if (p->crown) dy -= p->crown->height - 2;           // ... and above the crown
    ftext_pin(a, dy, buf, bd->font, p->level_tag_palette);
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
        vars_add(p->hp_max_var, p->hp_per_level);
        if (p->hp_max_var >= 0) vars_set(p->hp_var, vars_get(p->hp_max_var));   // a full heal: fighting pays off
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

static void par_off(Actor *a) {
    a->flags &= (u8)~ACTOR_STILL;
    a->draw_dx = 0;
}

static void faint(Actor *a, const Params_monster *p, Monster *m) {
    m->status = 0;
    par_off(a);                         // its fainting animation plays
    m->buff = 0;
    m->shove_ticks = 0;
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
    tag_off(a, m);
    s32 hp = hp_of(p, m) - amount;
    if (hp < 0) hp = 0;
    if (p->hp_var >= 0) vars_set(p->hp_var, hp);
    else m->hp = (s16)hp;
    const Params_monster *pp = m->team == TEAM_WILD ? params_of(player_monster()) : NULL;
    if (pp) give_xp(pp->xp_per_hit);
    if (hp == 0) faint(a, p, m);
}

// Push a monster up to `px` pixels away from (fx, fy), stopping at walls and other monsters.
static void push(Actor *a, s32 fx, s32 fy, int px) {
    s32 dx = fx_to_int(a->x) - fx, dy = fx_to_int(a->y) - fy;
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    fixed sx = ax * 2 >= ay ? fx_from_int(fx_sign(dx)) : 0, sy = ay * 2 >= ax ? fx_from_int(fx_sign(dy)) : 0;
    if (physics_free_at(a, a->x + sx * px, a->y + sy * px)) {   // nothing in the way (the usual case)
        a->x += sx * px;
        a->y += sy * px;
        return;
    }
    for (int i = 1; i < px && physics_free_at(a, a->x + sx, a->y + sy); i++) {
        a->x += sx;
        a->y += sy;
    }
}

// A move's push: start sliding `px` pixels straight away from (fx, fy) in `ticks` ticks, fast at
// first, then slowing down (the k-th last tick slides k units: they add up to px).
static void shove(Actor *a, Monster *m, s32 fx, s32 fy, int px, int ticks) {
    s32 dx = fx_to_int(a->x) - fx, dy = fx_to_int(a->y) - fy;
    if (!dx && !dy) { dx = -a->look_x; dy = -a->look_y; }  // right on it: back off from where it looks
    if (!dx && !dy) dy = 1;
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy) << 8);    // the distance x 16
    s32 unit = ((s32)px << 17) / (ticks * (ticks + 1));        // 2 px / (ticks (ticks + 1)), in 1/65536 px
    m->shove_x = ((dx << 12) / len) * unit >> 8;               // (dx / distance in 24.8) x unit
    m->shove_y = ((dy << 12) / len) * unit >> 8;
    m->shove_ticks = (u16)ticks;
}

// One tick of a push: slide (along a wall it meets), or hold where something is in the way.
static void shove_step(Actor *a, Monster *m) {
    s32 k = m->shove_ticks--;
    fixed sx = (m->shove_x * k) >> 8, sy = (m->shove_y * k) >> 8;
    if (physics_free_at(a, a->x + sx, a->y + sy)) { a->x += sx; a->y += sy; }
    else if (sx && physics_free_at(a, a->x + sx, a->y)) a->x += sx;
    else if (sy && physics_free_at(a, a->x, a->y + sy)) a->y += sy;
}

// Its word floats up on `row` (above the hit's words); paralysis also zaps (game.battle sounds.par).
static void set_status(Actor *t, Monster *m, int status, int ticks, int row) {
    m->status = (u8)status;
    m->status_ticks = (u16)ticks;
    m->status_clock = 0;
    m->tox_step = 0;
    if (status == STATUS_PAR) { say(t, BT_PAR, NULL, row); battle_sound(BS_PAR); }
    else if (status == STATUS_PSN) say(t, BT_PSN, NULL, row);
    else if (status == STATUS_TOX) say(t, BT_TOX, NULL, row);
}

// A paralyzed monster shows it the whole time: it stops dead in the pose it had (its animation
// holds), trembles and sparks crackle over it (game.battle par_effect, looped on its middle); the
// yellow flicker is in the status code.
static void par_look(Actor *a, Monster *m) {
    a->flags |= ACTOR_STILL;
    a->draw_dx = (s8)(((core_frame() >> 1) & 1) * PAR_TREMBLE);
    const AnimationData *fx = g_game.battle->par_effect;
    if (!fx || !fx->frame_count || !a->body || (a->flags & ACTOR_HIDDEN)) return;
    u32 t = m->status_clock++, len = (u32)anim_length(fx);
    if (m->status_clock >= len) m->status_clock = 0;
    const AnimFrame *f = fx->frames;
    while (t >= f->ticks && f < fx->frames + fx->frame_count - 1) t -= f++->ticks;
    const SpriteData *s = fx->sprite;
    sprites_queue(s, f->frame, a, a->hb_x + a->hb_w / 2 - s->width / 2, a->hb_y + a->hb_h / 2 - s->height / 2,
                  f->flip, NULL);
}

// ---- the walking dodge ---------------------------------------------------------------------
// Which way it walked in the last tick: its last physics step (x - prev_x), recorded once per
// frame and before a hit can push it this frame, so only its own walking counts.
static void walk_record(const Actor *a, const Params_monster *p, Monster *m) {
    if (!p->dodge_walk && !p->dodge_away) return;       // most monsters don't dodge by walking
    u32 f = core_frame();
    if (m->walk_frame == f) return;
    m->walk_frame = f;
    fixed dx = a->x - a->prev_x, dy = a->y - a->prev_y;
    for (int i = 0; i < 4; i++) m->walked[i] <<= 1;
    if (dx < 0) m->walked[WALK_LEFT] |= 1;
    if (dx > 0) m->walked[WALK_RIGHT] |= 1;
    if (dy < 0) m->walked[WALK_UP] |= 1;
    if (dy > 0) m->walked[WALK_DOWN] |= 1;
}

// Accuracy points a move loses against t for its walking in the last dodge_ticks ticks before
// the hit: dodge_walk if it walked at all, + dodge_away if on one of those ticks it walked away
// from the user on both axes (diagonally away; on an axis where their middles are level, either way).
static s32 walk_dodge(const Actor *t, const Params_monster *p, const Monster *m, const MonsterHit *h) {
    if (!p->dodge_walk && !p->dodge_away) return 0;
    const u16 *w = m->walked;
    u32 window = (1u << p->dodge_ticks) - 1;
    if (!((w[WALK_LEFT] | w[WALK_RIGHT] | w[WALK_UP] | w[WALK_DOWN]) & window)) return 0;
    s32 dx = h->mid_x - (fx_to_int(t->x) + t->hb_x + t->hb_w / 2);    // where the user is
    s32 dy = h->mid_y - (fx_to_int(t->y) + t->hb_y + t->hb_h / 2);
    u32 ax = dx > 0 ? w[WALK_LEFT] : dx < 0 ? w[WALK_RIGHT] : (u32)(w[WALK_LEFT] | w[WALK_RIGHT]);
    u32 ay = dy > 0 ? w[WALK_UP] : dy < 0 ? w[WALK_DOWN] : (u32)(w[WALK_UP] | w[WALK_DOWN]);
    return p->dodge_walk + ((ax & ay & window) ? p->dodge_away : 0);
}

int monster_hit(Actor *t, const MonsterHit *h) {
    Monster *m = mon(t);
    const BattleData *b = g_game.battle;
    const MoveData *mv = h->move;
    int result = HIT_NONE;
    if (!m || !b || !actor_alive(t)) return HIT_NONE;
    tag_off(t, m);
    const Params_monster *tp = params_of(t);
    walk_record(t, tp, m);              // before this hit (a zone's too) pushes it
    if (mv->power > 0) {
        // It hits on accuracy %: the move's, + the user's aim, - the target's dodge and walking
        // dodge (zones never miss).
        s32 acc = (s32)mv->accuracy + h->aim - var_or_0(tp->dodge_var);
        if (h->roll) acc -= walk_dodge(t, tp, m, h);
        if (h->roll && acc < 100 && rng_range(1, 100) > acc) {
            say(t, BT_MISS, NULL, 0);
            return HIT_MISS;
        }
        // damage = power x (1 + 0.15 x (level - 1)) x type x user power, rounded, at least 1
        int pct = b->chart[mv->type * b->type_count + m->species->type];
        s32 d = (s32)mv->power * (100 + 15 * (h->level - 1));          // x 100
        d = d * pct / 100 * h->power / 100;
        d = (d + 50) / 100;
        if (d < 1) d = 1;
        say_number(t, BT_DAMAGE, d);
        if (pct > 100) { say(t, BT_SUPER, NULL, 1); result = HIT_SUPER; }
        else if (pct < 100) { say(t, BT_WEAK, NULL, 1); result = HIT_WEAK; }
        else result = HIT_NORMAL;
        t->flash_pal = hit_palette(tp);
        t->flash = (u8)tp->flash_ticks;
        m->flashes = (u8)(tp->flash_ticks > 0 && tp->flashes > 1 ? tp->flashes - 1 : 0);
        m->flash_gap = 0;
        push(t, h->from_x, h->from_y, PUSH_PX);
        scripts_start(tp->on_hurt, t);
        hurt(t, d);
        if (!actor_alive(t)) return result;
    }
    if (mv->status != STATUS_NONE && rng_range(1, 100) <= mv->status_chance)
        set_status(t, m, mv->status, rng_range(mv->status_min, mv->status_max),
                   result == HIT_SUPER || result == HIT_WEAK ? 2 : 1);
    if (mv->push_px) shove(t, m, h->from_x, h->from_y, mv->push_px, mv->push_ticks);
    return result;
}

void monster_hit_sound(const MonsterHit *h, int result) {
    const BattleData *b = g_game.battle;
    if (!b || result == HIT_NONE) return;
    if (result == HIT_MISS) { battle_sound(BS_MISS); return; }
    if (h->team == TEAM_PLAYER && b->player_hits) {     // the player's hits sound like their type
        const SfxData *s = b->player_hits[h->move->type];
        if (s) audio_play_sfx(s);
        else battle_sound(BS_HIT);
        if (result == HIT_SUPER) battle_sound(BS_SUPER);
        return;
    }
    battle_sound(result == HIT_SUPER ? BS_SUPER : result == HIT_WEAK ? BS_WEAK : BS_HIT);
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
    a->flash_pal = hit_palette(p);
}

void bhv_monster_update(Actor *a, const void *params) {
    const Params_monster *p = params;
    Monster *m = mon(a);
    if (!m) return;
    walk_record(a, p, m);               // before anything this frame can push it
    moves_tick();                       // ground zones (once per frame, whoever asks first)
    if (p->level_var >= 0) {
        if (p->xp_var >= 0) player_progress(a, p, m);
        else m->level = (u8)vars_get(p->level_var);
    }
    if (m->flashes && a->flash == 0 && ++m->flash_gap >= p->flash_ticks) {   // a hit's next flash
        m->flashes--;
        m->flash_gap = 0;
        a->flash_pal = hit_palette(p);
        a->flash = (u8)(p->flash_ticks + 1);       // counted down at the end of this tick
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
            a->flash_pal = hit_palette(p);
        }
        if (m->status_ticks && --m->status_ticks == 0) m->status = STATUS_NONE;
    }
    if (m->status == STATUS_PAR) par_look(a, m);
    else if (a->flags & ACTOR_STILL) par_off(a);
    if (m->shove_ticks) shove_step(a, m);
    if (m->buff) buff_update(a, p, m);
    if (p->crown && a->body) {
        int bob = (core_frame() >> 4) & 1;
        sprites_queue(p->crown, (u16)bob, a, -p->crown->width / 2,
                      -a->body->origin_y - p->crown->height + 3 - bob, 0, NULL);
    }
    if (p->level_tag) level_tag(a, p, m);
    if (p->aura && (core_frame() % 24) == (u32)(actor_index(a) % 24))
        particles_burst(p->aura, fx_to_int(a->x) + rng_range(-8, 8), fx_to_int(a->y) - rng_range(2, 14));
}
