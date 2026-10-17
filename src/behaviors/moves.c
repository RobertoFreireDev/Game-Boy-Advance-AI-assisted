// moves - targets, effects and ground zones of the moves (see moves.h).
#include "moves.h"
#include "monster.h"
#include "engine/actions.h"
#include "engine/audio.h"
#include "engine/camera.h"
#include "engine/config.h"
#include "engine/core.h"
#include "engine/physics.h"
#include "engine/particles.h"
#include "engine/scene.h"

#define MAX_TARGETS 16

typedef struct {
    const MoveData *move;
    u8 active, team, level;
    u8 use;                     // which use of a move placed it: the zones of one use hurt a foe once a frame
    u16 power;
    s16 x, y, w, h;             // world pixels (top-left, size)
    s16 hl, ht, hr, hb;         // what it hurts: its effect's picture + the margin (world px, inclusive)
    u16 delay, ticks;
    u16 clock;                  // ticks to the next flicker of its effect
    u16 age;                    // ticks since it landed
    u16 arrive;                 // frames from its use until the travel picture lands on it: its first flicker waits (0 = none)
    u32 used;                   // core frame of its use
} Zone;

EWRAM_BSS static Zone s_zones[MAX_ZONES];        // cleared when a scene starts
// Per zone and actor: the zone's age + 1 when it last hurt that actor (0 = not yet). Each
// monster has its own timer: hurt the moment it steps on, then every `every` ticks while it stays.
EWRAM_BSS static u16 s_zone_hurt_at[MAX_ZONES][MAX_ACTORS];
static u32 s_zone_frame;
static u16 s_zone_load;         // scene_loads() when the zones were last cleared
static u8 s_zone_use;           // counts the uses that placed zones
static int s_power_now;         // this use's damage in percent, instead of the user's own (0 = its own)

// The strike: a move hurts what its effect draws. Its particles are picked first (particles_aim),
// so before they are sent off the move knows every spot each one will be drawn at, and it hurts
// every foe whose body comes within MOVE_HIT_MARGIN px of the effect's picture (fx_l..fx_b
// around a particle) at any of those spots.
typedef struct {
    ParticleShot shot;
    s32 l, t, r, b;             // what its picture covers over its whole flight (world px, inclusive)
} Piece;

EWRAM_BSS static Piece s_pieces[MAX_PARTICLES];
EWRAM_BSS static Actor *s_hit[MAX_ACTORS];     // the foes the last use hit (not a miss): a double hit's targets
EWRAM_BSS static int s_hit_count;
static int s_piece_count;
static s32 s_strike[4];         // the box around every piece: left, top, right, bottom

// Pixels of gap from a's body to b's along one axis (a spans a0..a1, b spans b0..b1): 0 when
// they touch, -1 while they overlap on it. Capped so that its square never overflows.
static s32 axis_gap(s32 a0, s32 a1, s32 b0, s32 b1) {
    s32 g = b0 > a1 ? b0 - a1 - 1 : a0 > b1 ? a0 - b1 - 1 : -1;
    return g > 4095 ? 4095 : g;
}

// The straight-line gap between two actors' bodies (hitboxes), squared; -1 while they overlap.
static s32 gap2(const Actor *a, const Actor *b) {
    s32 al = fx_to_int(a->x) + a->hb_x, at = fx_to_int(a->y) + a->hb_y;
    s32 bl = fx_to_int(b->x) + b->hb_x, bt = fx_to_int(b->y) + b->hb_y;
    s32 gx = axis_gap(al, al + a->hb_w - 1, bl, bl + b->hb_w - 1);
    s32 gy = axis_gap(at, at + a->hb_h - 1, bt, bt + b->hb_h - 1);
    if (gx < 0 && gy < 0) return -1;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    return gx * gx + gy * gy;
}

// Is a squared gap (gap2) within `tiles` tiles? 1 = touching, then 1 more every 8 px of gap,
// half a tile rounding up: the gap must be under 8 * tiles - 4 px. A circle, without a square root.
static int gap_within(s32 g2, int tiles) {
    if (tiles <= 0) return g2 < 0;
    s32 r = 8 * tiles - 4;
    return g2 < r * r;
}

int moves_within(const Actor *a, const Actor *b, int tiles) {
    return gap_within(gap2(a, b), tiles);
}

// Tiles between two actors' bodies (0 = overlapping), measured like moves_within. Counts up
// from 0, so it is only used on a target already in range (a few steps).
static int tiles_apart(const Actor *a, const Actor *b) {
    s32 g2 = gap2(a, b);
    int k = 0;
    while (!gap_within(g2, k)) k++;
    return k;
}

int moves_reach(const MoveData *m) {
    switch (m->shape) {
    case SHAPE_DASH: return m->range + m->dash;
    case SHAPE_ZONE: return m->zone_place == ZONE_FRONT ? 1 : m->zone_place == ZONE_AROUND ? m->range : 8;
    case SHAPE_SCREEN: return 8;
    case SHAPE_SELF: return 0;
    }
    return m->range;
}

// The middle of a monster's body (its art stands on the origin).
static s32 mid_y(const Actor *a) { return fx_to_int(a->y) - 7; }

// The middle of the tile k tiles out from the user's body, in direction (lx, ly): 8k - 4 px
// past its edge, or past its corner on a diagonal (the same distance: reach is a circle).
static void ahead(const Actor *u, int lx, int ly, int k, s32 *x, s32 *y) {
    s32 d = 8 * k - 4;
    if (lx && ly) d = (d * 181 + 128) >> 8;      // d / sqrt(2) on each axis, rounded
    *x = fx_to_int(u->x) + lx * (u->hb_w / 2 + d);
    *y = mid_y(u) + ly * (u->hb_h / 2 + d);
}

// The 8-way direction from u to t (like monster_ai's face_point); unchanged when they meet.
static void toward(const Actor *u, const Actor *t, int *lx, int *ly) {
    s32 dx = fx_to_int(t->x) - fx_to_int(u->x), dy = fx_to_int(t->y) - fx_to_int(u->y);
    s32 ax = fx_abs(dx), ay = fx_abs(dy);
    if (!dx && !dy) return;
    *lx = ax * 2 >= ay ? fx_sign(dx) : 0;
    *ly = ay * 2 >= ax ? fx_sign(dy) : 0;
}

// Is a monster's middle in the camera view at (cx, cy)? (where screen zones aim)
static int on_screen(const Actor *t, s32 cx, s32 cy) {
    s32 x = fx_to_int(t->x) - cx, y = fx_to_int(t->y) - cy;
    return x >= 0 && x < 240 && y >= 8 && y < 168;
}

// Is any of a monster's body in the camera view, give or take the margin? (what a whole-screen
// move hurts: its picture covers the view)
static int in_view(const Actor *t) {
    s32 l, tp, r, b, cx = camera_x(), cy = camera_y();
    actor_rect(t, &l, &tp, &r, &b);
    return r >= cx - MOVE_HIT_MARGIN && l < cx + 240 + MOVE_HIT_MARGIN &&
           b >= cy - MOVE_HIT_MARGIN && tp < cy + 160 + MOVE_HIT_MARGIN;
}

// The foe whose body is nearest the user's, within `range` tiles (a circle), or NULL.
static Actor *nearest_foe(Actor *user, int range) {
    Actor *best = NULL;
    s32 bd = 0x7fffffff;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *t = &g_actors[i];
        if (!t->active || !monster_is_foe(user, t)) continue;
        s32 d = gap2(user, t);
        if (d < bd && gap_within(d, range)) { bd = d; best = t; }
    }
    return best;
}

// (The cheap test first: most actors are off screen or not monsters.)
static int any_foe_on_screen(Actor *user) {
    s32 cx = camera_x(), cy = camera_y();
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && on_screen(&g_actors[i], cx, cy) && monster_is_foe(user, &g_actors[i])) return 1;
    return 0;
}

static void fx(const MoveData *m, s32 x, s32 y) {
    if (m->effect) particles_burst(m->effect, x, y);
}

// ---- the strike --------------------------------------------------------------------------
static int still(const ParticleShot *s) { return !s->vx && !s->vy && !s->data->gravity; }

// A particle in flight, moved like particles_update moves it (drawn after steps 1 .. life - 1).
typedef struct { fixed x, y, vy; } Flight;

static void flight_start(Flight *f, const ParticleShot *s) {
    f->x = fx_from_int(s->x);
    f->y = fx_from_int(s->y);
    f->vy = s->vy;
}

static void flight_step(Flight *f, const ParticleShot *s) {
    f->vy += s->data->gravity;
    f->x += s->vx;
    f->y += f->vy;
}

static void strike_begin(void) {
    s_piece_count = 0;
    s_strike[0] = s_strike[1] = 0x7fffffff;
    s_strike[2] = s_strike[3] = -0x7fffffff;
}

// Plan the move's effect (one burst) at a world position: pick its particles and the box each
// one's picture covers on its way.
static void plan(const MoveData *m, s32 x, s32 y) {
    const ParticleData *d = m->effect;
    if (!d) return;
    int n = d->mode == PTC_BURST ? d->count : 1;
    for (int i = 0; i < n && s_piece_count < MAX_PARTICLES; i++) {
        Piece *p = &s_pieces[s_piece_count++];
        particles_aim(d, i, x, y, &p->shot);
        p->l = p->t = 0x7fffffff;                       // until it is found drawn: touches no one
        p->r = p->b = -0x7fffffff;
        if (m->fx_l > m->fx_r) continue;                // an effect that draws nothing
        s32 x0 = 0x7fffffff, y0 = 0x7fffffff, x1 = -0x7fffffff, y1 = -0x7fffffff;
        Flight f;
        flight_start(&f, &p->shot);
        for (int k = 1; k < p->shot.life; k++) {        // the spots it is drawn at: lowest and highest
            flight_step(&f, &p->shot);
            s32 px = fx_to_int(f.x), py = fx_to_int(f.y);
            if (px < x0) x0 = px;
            if (px > x1) x1 = px;
            if (py < y0) y0 = py;
            if (py > y1) y1 = py;
            if (still(&p->shot)) break;                 // standing still: one spot is all of them
        }
        if (x0 > x1) continue;                          // gone before it is ever drawn
        p->l = x0 + m->fx_l;
        p->t = y0 + m->fx_t;
        p->r = x1 + m->fx_r;
        p->b = y1 + m->fx_b;
        if (p->l < s_strike[0]) s_strike[0] = p->l;
        if (p->t < s_strike[1]) s_strike[1] = p->t;
        if (p->r > s_strike[2]) s_strike[2] = p->r;
        if (p->b > s_strike[3]) s_strike[3] = p->b;
    }
}

// Plan the effect of a move aimed at foe t: on it, or a trail from the user to it.
static void plan_at(Actor *user, const MoveData *m, const Actor *t) {
    s32 ux = fx_to_int(user->x), uy = mid_y(user);
    s32 x = fx_to_int(t->x), y = mid_y(t);
    if (m->effect_at == FX_AT_LINE) {
        int steps = tiles_apart(user, t);
        if (steps < 1) steps = 1;
        for (int s = 1; s <= steps; s++) plan(m, ux + (x - ux) * s / steps, uy + (y - uy) * s / steps);
    } else {
        plan(m, x, y);
    }
}

// Plan the effect of a move aimed at no one: where its target would be, `range` tiles out
// in direction (lx, ly) (a trail: on every tile up to there).
static void plan_ahead(Actor *user, const MoveData *m, int lx, int ly) {
    int range = m->range > 0 ? m->range : 1;
    s32 x, y;
    for (int s = m->effect_at == FX_AT_LINE ? 1 : range; s <= range; s++) {
        ahead(user, lx, ly, s, &x, &y);
        plan(m, x, y);
    }
}

// Plan the effects that don't follow a target: at the user, on each tile along its facing, or
// down the screen's left edge.
static void plan_area(Actor *user, const MoveData *m) {
    s32 x = fx_to_int(user->x), y = mid_y(user);
    if (m->effect_at == FX_AT_USER) {
        plan(m, x, y);
    } else if (m->effect_at == FX_AT_AXIS) {
        for (int k = 1; k <= m->range; k++) {
            ahead(user, user->look_x, user->look_y, k, &x, &y);
            plan(m, x, y);
        }
    } else if (m->effect_at == FX_AT_SCREEN) {
        for (int k = 0; k < 5; k++) plan(m, camera_x() + 8, camera_y() + 20 + 32 * k);
    }
}

// Send off every planned particle (they stay planned until the next strike_begin).
static void strike_shoot(void) {
    for (int i = 0; i < s_piece_count; i++) particles_shoot(&s_pieces[i].shot);
}

// ---- the effect's 3 steps ----------------------------------------------------------------
// Only a picture: who is hurt, and when, is decided above and stays the same. In the time its
// effect lasted (its lifetime), a move plays: step 1 (m->step1 frames) its travel picture comes
// out from behind the user, step 2 (m->step2) it flies to where the effect plays, step 3 (the
// rest) the effect plays there, sped up so that it still ends when it did. Pieces of a trail or a
// beam show up as the travel passes them; a piece whose picture crosses the user's body on its way
// is drawn behind it, so the user stays in sight. An effect that plays at the user comes out from
// behind it (drawn behind it while it flies its whole way in steps 1-2) and stays there in front
// for step 3. Whole-screen moves (step1 = step2 = 0) play as they are.

// The middle of an actor's body (its hitbox).
static void body_mid(const Actor *a, s32 *x, s32 *y) {
    *x = fx_to_int(a->x) + a->hb_x + a->hb_w / 2;
    *y = fx_to_int(a->y) + a->hb_y + a->hb_h / 2;
}

// Where the travel picture has just come out of the user's body, on the way from the body's
// middle (cx, cy) to (ex, ey): the first point where the whole picture is past the body's edge
// (the destination itself when it is nearer).
static void travel_out(const Actor *u, const MoveData *m, s32 cx, s32 cy, s32 ex, s32 ey, s32 *ox, s32 *oy) {
    const SpriteData *s = m->travel->sprite;
    s32 dx = ex - cx, dy = ey - cy, t = FX_ONE;         // how far along (24.8)
    if (dx) {
        s32 k = ((u->hb_w / 2 + s->width / 2) << FX_SHIFT) / fx_abs(dx);
        if (k < t) t = k;
    }
    if (dy) {
        s32 k = ((u->hb_h / 2 + s->height / 2) << FX_SHIFT) / fx_abs(dy);
        if (k < t) t = k;
    }
    *ox = cx + ((dx * t) >> FX_SHIFT);
    *oy = cy + ((dy * t) >> FX_SHIFT);
}

// Does the box l..b (world px, inclusive) cover any of the actor's body?
static int over_body(const Actor *a, s32 l, s32 t, s32 r, s32 b) {
    s32 al, at, ar, ab;
    actor_rect(a, &al, &at, &ar, &ab);
    return l <= r && r >= al && l <= ar && b >= at && t <= ab;
}

// Send off the planned particles in 3 steps (see above).
static void strike_show(Actor *user, const MoveData *m) {
    int t1 = m->step1, t2 = m->step2;
    if (t1 + t2 == 0) { strike_shoot(); return; }
    if (m->effect_at == FX_AT_USER) {
        for (int i = 0; i < s_piece_count; i++) particles_shoot_out(&s_pieces[i].shot, t1 + t2);
        return;
    }
    if (!m->travel || !s_piece_count) { strike_shoot(); return; }
    // The travel goes from the user to the farthest piece (the target, a trail's or a beam's end).
    s32 cx, cy, ex, ey, ox, oy, far = -1;
    body_mid(user, &cx, &cy);
    ex = cx;
    ey = cy;
    for (int i = 0; i < s_piece_count; i++) {
        const ParticleShot *s = &s_pieces[i].shot;
        s32 dx = s->x - cx, dy = s->y - cy;
        if (dx * dx + dy * dy > far) { far = dx * dx + dy * dy; ex = s->x; ey = s->y; }
    }
    travel_out(user, m, cx, cy, ex, ey, &ox, &oy);
    particles_travel(m->travel, cx, cy, ox, oy, ex, ey, t1, t2);
    s32 wx = ex - ox, wy = ey - oy, len2 = wx * wx + wy * wy;
    for (int i = 0; i < s_piece_count; i++) {
        const Piece *p = &s_pieces[i];
        const ParticleShot *s = &p->shot;
        int wait = t1 + t2;                             // it shows once the travel has passed it
        if (len2) {
            s32 along = (s->x - ox) * wx + (s->y - oy) * wy;
            if (along <= 0) wait = t1;
            else if (along < len2) wait = t1 + (along * t2 + len2 - 1) / len2;
        }
        particles_shoot_late(s, wait, over_body(user, p->l, p->t, p->r, p->b));    // its flight crosses the user
    }
}

// Does a piece's picture, somewhere along its flight, come into the box l..b?
static int piece_touches(const Piece *p, const MoveData *m, s32 l, s32 t, s32 r, s32 b) {
    if (l > p->r || r < p->l || t > p->b || b < p->t) return 0;
    if (still(&p->shot)) return 1;
    Flight f;
    flight_start(&f, &p->shot);
    for (int k = 1; k < p->shot.life; k++) {
        flight_step(&f, &p->shot);
        s32 x = fx_to_int(f.x), y = fx_to_int(f.y);
        if (x + m->fx_r >= l && x + m->fx_l <= r && y + m->fx_b >= t && y + m->fx_t <= b) return 1;
    }
    return 0;
}

// Does the move reach t: does its picture come within the margin of t's body (a whole-screen
// move: is t in view)?
static int reaches(const MoveData *m, const Actor *t) {
    if (m->shape == SHAPE_SCREEN) return in_view(t);
    s32 l, tp, r, b;
    actor_rect(t, &l, &tp, &r, &b);
    l -= MOVE_HIT_MARGIN;
    tp -= MOVE_HIT_MARGIN;
    r += MOVE_HIT_MARGIN;
    b += MOVE_HIT_MARGIN;
    if (l > s_strike[2] || r < s_strike[0] || tp > s_strike[3] || b < s_strike[1]) return 0;
    for (int i = 0; i < s_piece_count; i++)
        if (piece_touches(&s_pieces[i], m, l, tp, r, b)) return 1;
    return 0;
}

// Does the planned move reach any foe? (a wild monster only uses a move that does)
static int reaches_a_foe(Actor *user, const MoveData *m) {
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *t = &g_actors[i];
        if (t->active && monster_is_foe(user, t) && reaches(m, t)) return 1;
    }
    return 0;
}

// Hurt `first` (the foe the move was aimed at, may be NULL) and every other foe the move
// reaches. Returns the best result; *n = how many it hurt (s_hit: the ones it didn't miss).
static int hurt(Actor *user, const MoveData *m, Actor *first, const MonsterHit *h, int *n) {
    int best = HIT_NONE;
    *n = 0;
    s_hit_count = 0;
    if (first) {
        best = monster_hit(first, h);
        *n = 1;
        if (best >= HIT_WEAK) s_hit[s_hit_count++] = first;
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *t = &g_actors[i];
        if (t == first || !t->active || !monster_is_foe(user, t) || !reaches(m, t)) continue;
        int res = monster_hit(t, h);
        if (res > best) best = res;
        if (res >= HIT_WEAK) s_hit[s_hit_count++] = t;
        (*n)++;
    }
    return best;
}

// Does the move's result make its sound? The player's damaging moves that hit at once play the
// hit (by type) or miss sound instead of their own.
static int result_sound_only(const Actor *user, const MoveData *m) {
    return monster_team(user) == TEAM_PLAYER && m->power > 0 && m->shape != SHAPE_ZONE && m->shape != SHAPE_SELF;
}

// The move goes off: sound, attack pose, its actions.
static void begin(Actor *user, const MoveData *m) {
    if (m->sfx && !result_sound_only(user, m)) audio_play_sfx(m->sfx);
    actor_play_slot(user, ANIM_ATTACK);
    user->anim_lock = 10;
    scripts_start(m->on_use, user);
}

static MonsterHit hit_from(Actor *user, const MoveData *m) {
    MonsterHit h;
    h.move = m;
    h.team = (u8)monster_team(user);
    h.level = (u8)monster_level(user);
    h.roll = 1;
    h.blow = BLOW_ONE;
    h.power = (u16)monster_power(user, s_power_now);
    h.aim = (s16)monster_aim(user);
    h.from_x = fx_to_int(user->x);
    h.from_y = fx_to_int(user->y);
    body_mid(user, &h.mid_x, &h.mid_y);
    return h;
}

// Rush up to `px` pixels toward a point, stopping at walls and monsters.
static void dash(Actor *u, s32 tx, s32 ty, int px) {
    s32 dx = tx - fx_to_int(u->x), dy = ty - fx_to_int(u->y);
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
    if (len == 0) return;
    fixed sx = dx * FX_ONE / len, sy = dy * FX_ONE / len;
    if (physics_free_at(u, u->x + sx * px, u->y + sy * px)) {      // the whole way is free
        u->x += sx * px;
        u->y += sy * px;
        return;
    }
    for (int i = 0; i < px; i++) {
        if (!physics_free_at(u, u->x + sx, u->y + sy)) break;
        u->x += sx;
        u->y += sy;
    }
}

// ---- zones -------------------------------------------------------------------------------
// How far a zone's cell x cell blocks overhang it on each side (px), along a side `len` px long:
// when it isn't a multiple of the block, the blocks are centered on it (0 when it is).
static s32 cell_overhang(s32 len, s32 size) {
    s32 span = size;
    while (span < len) span += size;
    return (span - len) / 2;
}

static Zone *zone_add(Actor *user, const MoveData *m, s32 tx, s32 ty) {
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &s_zones[i];
        if (z->active) continue;
        z->active = 1;
        z->move = m;
        z->team = (u8)monster_team(user);
        z->level = (u8)monster_level(user);
        z->use = s_zone_use;
        z->power = (u16)monster_power(user, s_power_now);
        z->x = (s16)((tx - m->zone_w / 2) * 8);
        z->y = (s16)((ty - m->zone_h / 2) * 8);
        z->w = (s16)(m->zone_w * 8);
        z->h = (s16)(m->zone_h * 8);
        // What it hurts: its effect's picture on each of its tiles (or cell x cell blocks), or at
        // its middle (as drawn while it can hurt: a falling rock where it lands), + the margin.
        // No picture: its tiles.
        s32 l = z->x, t = z->y, r = z->x + z->w - 1, b = z->y + z->h - 1;
        if (m->effect && m->fx_l <= m->fx_r) {
            if (m->effect_at == FX_AT_TILES) {
                s32 half = m->zone_cell * 4;            // from a block's edge to its middle
                s32 ox = cell_overhang(z->w, half * 2), oy = cell_overhang(z->h, half * 2);
                l = z->x - ox + half + m->fx_l;
                t = z->y - oy + half + m->fx_t;
                r = z->x + z->w + ox - half + m->fx_r;
                b = z->y + z->h + oy - half + m->fx_b;
            } else if (m->effect_at == FX_AT_CENTER) {
                s32 cx = z->x + z->w / 2, cy = z->y + z->h / 2;
                l = cx + m->fx_l;
                t = cy + m->fx_t;
                r = cx + m->fx_r;
                b = cy + m->fx_b;
            }
        }
        z->hl = (s16)(l - MOVE_HIT_MARGIN);
        z->ht = (s16)(t - MOVE_HIT_MARGIN);
        z->hr = (s16)(r + MOVE_HIT_MARGIN);
        z->hb = (s16)(b + MOVE_HIT_MARGIN);
        z->delay = m->zone_delay;
        z->ticks = (u16)rng_range(m->ticks_min, m->ticks_max);
        if (z->ticks == 0) z->ticks = 1;
        z->clock = 0;
        z->age = 0;
        z->arrive = 0;
        z->used = core_frame();
        for (int k = 0; k < MAX_ACTORS; k++) s_zone_hurt_at[i][k] = 0;
        if (m->effect_at == FX_AT_CENTER) fx(m, z->x + z->w / 2, z->y + z->h / 2);
        return z;
    }
    return NULL;
}

// Where the zones of a move go: the tiles in front of the user, the tiles around it (its
// middle tile in the zone's middle), or spots on the screen (each one on a monster in view when
// there is one, else anywhere in view).
static void zones_place(Actor *user, const MoveData *m) {
    s_zone_use++;
    s32 ux = fx_to_int(user->x) >> 3, uy = (fx_to_int(user->y) - 4) >> 3;
    if (m->zone_place == ZONE_AROUND) {
        s32 cx, cy;
        body_mid(user, &cx, &cy);
        zone_add(user, m, cx >> 3, cy >> 3);
        return;
    }
    if (m->zone_place == ZONE_FRONT) {              // starts just past the body's edge it faces
        s32 l, t, r, b;
        actor_rect(user, &l, &t, &r, &b);
        if (user->look_x) ux = (user->look_x > 0 ? r : l) >> 3;
        if (user->look_y) uy = (user->look_y > 0 ? b : t) >> 3;
        int ahead = 1 + m->zone_w / 2;
        Zone *z = zone_add(user, m, ux + user->look_x * ahead, uy + user->look_y * ahead);
        if (z && m->travel && m->step1 + m->step2) {    // its travel picture lands on its middle
            s32 cx, cy, ex = z->x + z->w / 2, ey = z->y + z->h / 2, ox, oy;
            body_mid(user, &cx, &cy);
            travel_out(user, m, cx, cy, ex, ey, &ox, &oy);
            particles_travel(m->travel, cx, cy, ox, oy, ex, ey, m->step1, m->step2);
            z->arrive = (u16)(m->step1 + m->step2);
        }
        return;
    }
    Actor *seen[MAX_TARGETS];
    int n = 0;
    s32 cx = camera_x(), cy = camera_y();
    for (int i = 0; i < MAX_ACTORS && n < MAX_TARGETS; i++)
        if (g_actors[i].active && on_screen(&g_actors[i], cx, cy) && monster_is_foe(user, &g_actors[i])) seen[n++] = &g_actors[i];
    for (int k = 0; k < m->zone_count; k++) {
        s32 tx, ty;
        if (n) {
            const Actor *t = seen[rng_range(0, n - 1)];
            tx = (fx_to_int(t->x) >> 3) + rng_range(-1, 1);
            ty = ((fx_to_int(t->y) - 4) >> 3) + rng_range(-1, 1);
        } else {
            tx = (camera_x() >> 3) + rng_range(2, 27);
            ty = (camera_y() >> 3) + rng_range(3, 17);
        }
        zone_add(user, m, tx, ty);
    }
}

// The hit a zone deals: the ground never misses, and pushes away from the middle of what it hurts.
static void zone_hit_info(const Zone *z, MonsterHit *h) {
    h->move = z->move;
    h->team = z->team;
    h->level = z->level;
    h->roll = 0;
    h->blow = BLOW_ONE;
    h->aim = 0;
    h->power = z->power;
    h->from_x = h->mid_x = (z->hl + z->hr) / 2;
    h->from_y = h->mid_y = (z->ht + z->hb) / 2;
}

// The zone's effect on each of its tiles, or in the middle of each cell x cell block of them
// (it flickers every `every` ticks while it lasts). The last flicker is cut short so that
// nothing is drawn once the zone stops hurting: it hurts this
// frame and z->ticks - 1 more, and a particle is drawn until its life - 1 steps have run.
// (A one-hit zone plays its whole effect.) The first flicker of a zone the move's travel picture
// flies to waits until it lands, then plays sped up (step 3), ending when it would have.
static void zone_fx(Zone *z) {
    const MoveData *m = z->move;
    const ParticleData *d = m->effect;
    if (m->effect_at != FX_AT_TILES || !d) return;
    int count = d->mode == PTC_BURST ? d->count : 1;
    int wait = 0;
    if (z->arrive) {
        u32 gone = core_frame() - z->used;              // 0, or 1 when the zones already ran this frame
        if (gone < z->arrive) wait = (int)(z->arrive - gone);
        z->arrive = 0;
    }
    int size = m->zone_cell * 8;
    s32 ox = cell_overhang(z->w, size), oy = cell_overhang(z->h, size);
    for (s32 y = -oy; y < z->h; y += size)
        for (s32 x = -ox; x < z->w; x += size)
            for (int i = 0; i < count; i++) {
                ParticleShot s;
                particles_aim(d, i, z->x + x + size / 2, z->y + y + size / 2, &s);
                if (m->zone_every && s.life > z->ticks + 1) s.life = (u16)(z->ticks + 1);
                if (wait) particles_shoot_late(&s, wait, 0);
                else particles_shoot(&s);
            }
}

void moves_tick(void) {
    u32 f = core_frame();
    if (s_zone_frame == f) return;
    s_zone_frame = f;
    u16 load = scene_loads();
    if (load != s_zone_load) {                   // a scene (re)loaded, e.g. a new run: no zones are left on the ground
        s_zone_load = load;
        for (int i = 0; i < MAX_ZONES; i++) s_zones[i].active = 0;
    }
    // 1. The zones on the ground this frame (landed, not waiting out their delay), and the box
    //    around all of them: most monsters are far from every zone and one test skips them.
    u8 live[MAX_ZONES];
    s8 best[MAX_ZONES];
    s32 zr[MAX_ZONES][4];                       // what each hurts: left, top, right, bottom (inclusive)
    s32 bl = 0x7fffffff, bt = 0x7fffffff, br = -0x7fffffff, bb = -0x7fffffff;
    int n = 0;
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &s_zones[i];
        if (!z->active) continue;
        if (z->delay) { z->delay--; continue; }
        if (z->clock == 0) {
            zone_fx(z);
            z->clock = z->move->zone_every;
        }
        s32 *e = zr[n];
        e[0] = z->hl; e[1] = z->ht; e[2] = z->hr; e[3] = z->hb;
        if (e[0] < bl) bl = e[0];
        if (e[1] < bt) bt = e[1];
        if (e[2] > br) br = e[2];
        if (e[3] > bb) bb = e[3];
        best[n] = HIT_NONE;
        live[n++] = (u8)i;
    }
    if (!n) return;
    // 2. Each monster the zone's picture touches is hurt, unless that zone hurt it less than `every` ago.
    //    Zones placed by one use (Rock Slide's rocks) hurt a monster they all touch once in a frame.
    for (int k = 0; k < MAX_ACTORS; k++) {
        Actor *t = &g_actors[k];
        if (!t->active) continue;
        s32 l = fx_to_int(t->x) + t->hb_x, r = l + t->hb_w - 1;      // like actor_rect, inline
        if (l > br || r < bl) continue;
        s32 tp = fx_to_int(t->y) + t->hb_y, b = tp + t->hb_h - 1;
        if (tp > bb || b < bt) continue;
        u8 hit_by[MAX_ZONES];                   // the uses that hurt it this frame
        int nhit = 0;
        for (int j = 0; j < n; j++) {
            const s32 *e = zr[j];
            if (l > e[2] || r < e[0] || tp > e[3] || b < e[1]) continue;
            Zone *z = &s_zones[live[j]];
            u16 *at = &s_zone_hurt_at[live[j]][k];
            if (*at && z->age + 1 - *at < z->move->zone_every) continue;   // still cooling down
            if (!monster_targetable(t) || monster_team(t) == z->team) continue;
            *at = (u16)(z->age + 1);
            int u = 0;
            while (u < nhit && hit_by[u] != z->use) u++;
            if (u < nhit) continue;             // another zone of the same use hurt it just now
            hit_by[nhit++] = z->use;
            MonsterHit h;
            zone_hit_info(z, &h);
            int res = monster_hit(t, &h);
            if (res > best[j]) best[j] = (s8)res;
        }
    }
    // 3. One result sound per zone that hurt someone; then its clocks run on.
    for (int j = 0; j < n; j++) {
        Zone *z = &s_zones[live[j]];
        MonsterHit h;
        zone_hit_info(z, &h);
        monster_hit_sound(&h, best[j]);
        if (z->move->zone_every == 0) { z->active = 0; continue; }       // one hit, then gone
        z->clock--;
        z->age++;
        if (--z->ticks == 0) z->active = 0;
    }
}

// ---- using a move -----------------------------------------------------------------------
int moves_use(Actor *user, const MoveData *m, int always) {
    if (!m || !monster_can_act(user)) return USE_NONE;
    MonsterHit h = hit_from(user, m);
    Actor *first = NULL;                                        // the foe it is aimed at
    strike_begin();
    switch (m->shape) {
    case SHAPE_SINGLE:
    case SHAPE_DASH: {
        first = nearest_foe(user, moves_reach(m));
        if (!first && !always) return USE_NONE;
        begin(user, m);
        int lx = user->look_x, ly = user->look_y;
        if (m->shape == SHAPE_DASH) {                           // rush at the foe, or straight ahead
            s32 ux = fx_to_int(user->x), uy = fx_to_int(user->y);
            dash(user, first ? fx_to_int(first->x) : ux + lx * 64,
                 first ? fx_to_int(first->y) : uy + ly * 64, m->dash * 8);
            h = hit_from(user, m);
            if (first && !moves_within(user, first, m->range)) {    // rushed in, but it got away: the
                toward(user, first, &lx, &ly);                      // blow lands in front, toward it
                first = NULL;
            }
        }
        if (first) plan_at(user, m, first);
        else plan_ahead(user, m, lx, ly);
        break;
    }
    case SHAPE_BEAM:
    case SHAPE_CIRCLE:
    case SHAPE_SCREEN:
        plan_area(user, m);
        if (!always && !reaches_a_foe(user, m)) return USE_NONE;
        begin(user, m);
        break;
    case SHAPE_ZONE:
        if (!always && (m->zone_place != ZONE_SCREEN ? !nearest_foe(user, m->range) : !any_foe_on_screen(user)))
            return USE_NONE;
        begin(user, m);
        zones_place(user, m);
        return USE_FIRED;
    case SHAPE_SELF:
        begin(user, m);
        plan_area(user, m);
        strike_show(user, m);
        monster_buff(user, m, rng_range(m->ticks_min, m->ticks_max));
        return USE_FIRED;
    default:
        return USE_NONE;
    }
    strike_show(user, m);
    if (m->power > 0 && monster_double_roll(user)) h.blow = BLOW_FIRST;     // a species that hits twice (Normal)
    int n;
    int best = hurt(user, m, first, &h, &n);
    if (!n) return result_sound_only(user, m) ? USE_WHIFF : USE_FIRED;     // went off at nothing
    monster_hit_sound(&h, best);
    monster_double_hit(user, s_hit, s_hit_count, &h);   // ... lands the same blow again on them
    return USE_FIRED;
}

int moves_use_power(Actor *user, const MoveData *m, int always, int power) {
    s_power_now = power;
    int r = moves_use(user, m, always);
    s_power_now = 0;
    return r;
}
