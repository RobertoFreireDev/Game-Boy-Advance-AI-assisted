// moves - targets, effects and ground zones of the moves (see moves.h).
#include "moves.h"
#include "monster.h"
#include "engine/actions.h"
#include "engine/audio.h"
#include "engine/camera.h"
#include "engine/core.h"
#include "engine/physics.h"
#include "engine/particles.h"
#include "engine/scene.h"

#define MAX_TARGETS 16

typedef struct {
    const MoveData *move;
    u8 active, team, level, pad;
    u16 power;
    s16 x, y, w, h;             // world pixels (top-left, size)
    u16 delay, ticks;
    u16 clock;                  // ticks to the next flicker of its effect
    u16 age;                    // ticks since it landed
} Zone;

EWRAM_BSS static Zone s_zones[MAX_ZONES];        // cleared when a scene starts
// Per zone and actor: the zone's age + 1 when it last hurt that actor (0 = not yet). Each
// monster has its own timer: hurt the moment it steps on, then every `every` ticks while it stays.
EWRAM_BSS static u16 s_zone_hurt_at[MAX_ZONES][MAX_ACTORS];
static u32 s_zone_frame;
static s16 s_zone_scene = -1;

// Round a pixel distance to tiles (half a tile rounds up).
static s32 tiles(s32 px) { return (px + 4) >> 3; }

// Tiles from a's body to b's along one axis (a spans a0..a1, b spans b0..b1): 0 while they
// overlap on it, 1 when they touch, then 1 more every 8 pixels of gap. Negative when b is
// on the low side (left / above).
static s32 axis_tiles(s32 a0, s32 a1, s32 b0, s32 b1) {
    if (b0 > a1) return tiles(b0 - a1 - 1) + 1;
    if (a0 > b1) return -tiles(a0 - b1 - 1) - 1;
    return 0;
}

// Where b's body is from a's, in tiles across (tx: + = right) and down (ty: + = below).
static void body_tiles(const Actor *a, const Actor *b, s32 *tx, s32 *ty) {
    s32 al = fx_to_int(a->x) + a->hb_x, at = fx_to_int(a->y) + a->hb_y;
    s32 bl = fx_to_int(b->x) + b->hb_x, bt = fx_to_int(b->y) + b->hb_y;
    *tx = axis_tiles(al, al + a->hb_w - 1, bl, bl + b->hb_w - 1);
    *ty = axis_tiles(at, at + a->hb_h - 1, bt, bt + b->hb_h - 1);
}

int moves_tiles(const Actor *a, const Actor *b) {
    s32 tx, ty;
    body_tiles(a, b, &tx, &ty);
    tx = fx_abs(tx);
    ty = fx_abs(ty);
    return tx > ty ? tx : ty;
}

int moves_reach(const MoveData *m) {
    switch (m->shape) {
    case SHAPE_DASH: return m->range + m->dash;
    case SHAPE_ZONE: return m->zone_place == ZONE_FRONT ? 1 : 8;
    case SHAPE_SCREEN: return 8;
    case SHAPE_SELF: return 0;
    }
    return m->range;
}

// The middle of a monster's body (its art stands on the origin).
static s32 mid_y(const Actor *a) { return fx_to_int(a->y) - 7; }

// The middle of the tile k tiles out from the user's body, along its facing.
static void ahead(const Actor *u, int k, s32 *x, s32 *y) {
    *x = fx_to_int(u->x) + u->look_x * (u->hb_w / 2 + 8 * k - 4);
    *y = mid_y(u) + u->look_y * (u->hb_h / 2 + 8 * k - 4);
}

static int on_screen(const Actor *t) {
    s32 x = fx_to_int(t->x) - camera_x(), y = fx_to_int(t->y) - camera_y();
    return x >= 0 && x < 240 && y >= 8 && y < 168;
}

// Is tile offset (tx, ty) inside a cone facing (lx, ly) that is 1 tile wide next to the user
// and 2 tiles wider every tile out? Diagonal cones cover the quarter between the two axes.
static int in_cone(s32 tx, s32 ty, int lx, int ly, int range) {
    if (lx && ly) {
        s32 a = tx * lx, b = ty * ly;
        if (a < 0 || b < 0) return 0;
        s32 far = a > b ? a : b;
        return far >= 1 && far <= range;
    }
    s32 along = lx ? tx * lx : ty * ly;
    s32 side = fx_abs(lx ? ty : tx);
    return along >= 1 && along <= range && side <= along - 1;
}

// Every foe of `user` that the move's area covers (up to MAX_TARGETS). Returns how many.
static int area_targets(Actor *user, const MoveData *m, Actor **out) {
    int n = 0;
    for (int i = 0; i < MAX_ACTORS && n < MAX_TARGETS; i++) {
        Actor *t = &g_actors[i];
        if (!t->active || !monster_is_foe(user, t)) continue;
        s32 tx, ty;
        body_tiles(user, t, &tx, &ty);
        int hit = 0;
        if (m->shape == SHAPE_CONE) hit = in_cone(tx, ty, user->look_x, user->look_y, m->range);
        else if (m->shape == SHAPE_CIRCLE) hit = moves_tiles(user, t) <= m->range;
        else if (m->shape == SHAPE_SCREEN) hit = on_screen(t);
        if (hit) out[n++] = t;
    }
    return n;
}

static Actor *nearest_foe(Actor *user, int range) {
    Actor *best = NULL;
    int bd = range + 1;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *t = &g_actors[i];
        if (!t->active || !monster_is_foe(user, t)) continue;
        int d = moves_tiles(user, t);
        if (d < bd) { bd = d; best = t; }
    }
    return best;
}

static int any_foe_on_screen(Actor *user) {
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && monster_is_foe(user, &g_actors[i]) && on_screen(&g_actors[i])) return 1;
    return 0;
}

static void fx(const MoveData *m, s32 x, s32 y) {
    if (m->effect) particles_burst(m->effect, x, y);
}

// Effects that follow the targets: at each one, or a trail from the user to each one.
static void fx_targets(Actor *user, const MoveData *m, Actor **t, int n) {
    s32 ux = fx_to_int(user->x), uy = mid_y(user);
    for (int k = 0; k < n; k++) {
        s32 x = fx_to_int(t[k]->x), y = mid_y(t[k]);
        if (m->effect_at == FX_AT_LINE) {
            int steps = moves_tiles(user, t[k]);
            if (steps < 1) steps = 1;
            for (int s = 1; s <= steps; s++) fx(m, ux + (x - ux) * s / steps, uy + (y - uy) * s / steps);
        } else if (m->effect_at == FX_AT_TARGET) {
            fx(m, x, y);
        }
    }
}

// Effects that don't need targets: at the user, along its facing, or across the screen.
static void fx_area(Actor *user, const MoveData *m) {
    s32 ux = fx_to_int(user->x), uy = mid_y(user);
    if (m->effect_at == FX_AT_USER) {
        fx(m, ux, uy);
    } else if (m->effect_at == FX_AT_AXIS) {
        for (int k = 1; k <= m->range; k++) {
            s32 x, y;
            ahead(user, k, &x, &y);
            fx(m, x, y);
        }
    } else if (m->effect_at == FX_AT_SCREEN) {
        for (int k = 0; k < 5; k++) fx(m, camera_x() + 8, camera_y() + 20 + 32 * k);
    }
}

// Effects of a move that reached no one: where its target would be, straight ahead at full range.
static void fx_ahead(Actor *user, const MoveData *m) {
    int range = m->range > 0 ? m->range : 1;
    s32 x, y;
    if (m->effect_at == FX_AT_LINE) {
        for (int s = 1; s <= range; s++) {
            ahead(user, s, &x, &y);
            fx(m, x, y);
        }
    } else if (m->effect_at == FX_AT_TARGET) {
        ahead(user, range, &x, &y);
        fx(m, x, y);
    }
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
    h.power = (u16)monster_power(user);
    h.from_x = fx_to_int(user->x);
    h.from_y = fx_to_int(user->y);
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
static void zone_add(Actor *user, const MoveData *m, s32 tx, s32 ty) {
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &s_zones[i];
        if (z->active) continue;
        z->active = 1;
        z->move = m;
        z->team = (u8)monster_team(user);
        z->level = (u8)monster_level(user);
        z->power = (u16)monster_power(user);
        z->x = (s16)((tx - m->zone_w / 2) * 8);
        z->y = (s16)((ty - m->zone_h / 2) * 8);
        z->w = (s16)(m->zone_w * 8);
        z->h = (s16)(m->zone_h * 8);
        z->delay = m->zone_delay;
        z->ticks = (u16)rng_range(m->ticks_min, m->ticks_max);
        if (z->ticks == 0) z->ticks = 1;
        z->clock = 0;
        z->age = 0;
        for (int k = 0; k < MAX_ACTORS; k++) s_zone_hurt_at[i][k] = 0;
        if (m->effect_at == FX_AT_CENTER) fx(m, z->x + z->w / 2, z->y + z->h / 2);
        return;
    }
}

// Where the zones of a move go: the tiles in front of the user, or spots on the screen
// (each one on a monster in view when there is one, else anywhere in view).
static void zones_place(Actor *user, const MoveData *m) {
    s32 ux = fx_to_int(user->x) >> 3, uy = (fx_to_int(user->y) - 4) >> 3;
    if (m->zone_place == ZONE_FRONT) {              // starts just past the body's edge it faces
        s32 l, t, r, b;
        actor_rect(user, &l, &t, &r, &b);
        if (user->look_x) ux = (user->look_x > 0 ? r : l) >> 3;
        if (user->look_y) uy = (user->look_y > 0 ? b : t) >> 3;
        int ahead = 1 + m->zone_w / 2;
        zone_add(user, m, ux + user->look_x * ahead, uy + user->look_y * ahead);
        return;
    }
    Actor *seen[MAX_TARGETS];
    int n = 0;
    for (int i = 0; i < MAX_ACTORS && n < MAX_TARGETS; i++)
        if (g_actors[i].active && monster_is_foe(user, &g_actors[i]) && on_screen(&g_actors[i])) seen[n++] = &g_actors[i];
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

// The hit a zone deals: the ground never misses, and pushes away from the zone's middle.
static void zone_hit_info(const Zone *z, MonsterHit *h) {
    h->move = z->move;
    h->team = z->team;
    h->level = z->level;
    h->roll = 0;
    h->power = z->power;
    h->from_x = z->x + z->w / 2;
    h->from_y = z->y + z->h / 2;
}

// The zone's effect on each of its tiles (it flickers every `every` ticks while it lasts).
static void zone_fx(const Zone *z) {
    const MoveData *m = z->move;
    if (m->effect_at != FX_AT_TILES) return;
    for (int y = 0; y < z->h; y += 8)
        for (int x = 0; x < z->w; x += 8) fx(m, z->x + x + 4, z->y + y + 4);
}

void moves_tick(void) {
    u32 f = core_frame();
    if (s_zone_frame == f) return;
    s_zone_frame = f;
    s16 scene = scene_current_node();
    if (scene != s_zone_scene) {                 // a new scene: no zones are left on the ground
        s_zone_scene = scene;
        for (int i = 0; i < MAX_ZONES; i++) s_zones[i].active = 0;
    }
    // 1. The zones on the ground this frame (landed, not waiting out their delay), and the box
    //    around all of them: most monsters are far from every zone and one test skips them.
    u8 live[MAX_ZONES];
    s8 best[MAX_ZONES];
    s32 zr[MAX_ZONES][4];                       // left, top, right, bottom (right / bottom excluded)
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
        e[0] = z->x; e[1] = z->y; e[2] = z->x + z->w; e[3] = z->y + z->h;
        if (e[0] < bl) bl = e[0];
        if (e[1] < bt) bt = e[1];
        if (e[2] > br) br = e[2];
        if (e[3] > bb) bb = e[3];
        best[n] = HIT_NONE;
        live[n++] = (u8)i;
    }
    if (!n) return;
    // 2. Each monster standing on a zone is hurt, unless that zone hurt it less than `every` ago.
    for (int k = 0; k < MAX_ACTORS; k++) {
        Actor *t = &g_actors[k];
        if (!t->active) continue;
        s32 l = fx_to_int(t->x) + t->hb_x, r = l + t->hb_w - 1;      // like actor_rect, inline
        if (l >= br || r < bl) continue;
        s32 tp = fx_to_int(t->y) + t->hb_y, b = tp + t->hb_h - 1;
        if (tp < b - 7) tp = b - 7;                 // the ground hurts the feet: the body's bottom 8 px
        if (tp >= bb || b < bt) continue;
        for (int j = 0; j < n; j++) {
            const s32 *e = zr[j];
            if (l >= e[2] || r < e[0] || tp >= e[3] || b < e[1]) continue;
            Zone *z = &s_zones[live[j]];
            u16 *at = &s_zone_hurt_at[live[j]][k];
            if (*at && z->age + 1 - *at < z->move->zone_every) continue;   // still cooling down
            if (!monster_targetable(t) || monster_team(t) == z->team) continue;
            *at = (u16)(z->age + 1);
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
    Actor *t[MAX_TARGETS];
    int empty = result_sound_only(user, m) ? USE_WHIFF : USE_FIRED;    // went off at nothing
    switch (m->shape) {
    case SHAPE_SINGLE:
    case SHAPE_DASH: {
        Actor *f = nearest_foe(user, moves_reach(m));
        if (!f && !always) return USE_NONE;
        begin(user, m);
        if (m->shape == SHAPE_DASH) {                           // rush at the foe, or straight ahead
            s32 ux = fx_to_int(user->x), uy = fx_to_int(user->y);
            dash(user, f ? fx_to_int(f->x) : ux + user->look_x * 64,
                 f ? fx_to_int(f->y) : uy + user->look_y * 64, m->dash * 8);
            h = hit_from(user, m);
            if (f && moves_tiles(user, f) > m->range) {         // rushed in, but it got away
                t[0] = f;
                fx_targets(user, m, t, 1);
                monster_hit_sound(&h, HIT_MISS);
                return USE_FIRED;
            }
        }
        if (!f) {
            fx_ahead(user, m);
            return empty;
        }
        t[0] = f;
        fx_targets(user, m, t, 1);
        monster_hit_sound(&h, monster_hit(f, &h));
        return USE_FIRED;
    }
    case SHAPE_CONE:
    case SHAPE_CIRCLE:
    case SHAPE_SCREEN: {
        int n = area_targets(user, m, t);
        if (!n && !always) return USE_NONE;
        begin(user, m);
        fx_area(user, m);
        if (!n) {
            fx_ahead(user, m);
            return empty;
        }
        fx_targets(user, m, t, n);
        int best = HIT_NONE;
        for (int k = 0; k < n; k++) {
            int res = monster_hit(t[k], &h);
            if (res > best) best = res;
        }
        monster_hit_sound(&h, best);
        return USE_FIRED;
    }
    case SHAPE_ZONE:
        if (!always && (m->zone_place == ZONE_FRONT ? !nearest_foe(user, m->range) : !any_foe_on_screen(user)))
            return USE_NONE;
        begin(user, m);
        fx_area(user, m);
        zones_place(user, m);
        return USE_FIRED;
    case SHAPE_SELF:
        begin(user, m);
        fx_area(user, m);
        monster_buff(user, m->buff, rng_range(m->ticks_min, m->ticks_max));
        return USE_FIRED;
    }
    return USE_NONE;
}
