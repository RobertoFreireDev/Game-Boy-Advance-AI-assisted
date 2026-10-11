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
    u16 delay, ticks, clock;
} Zone;

EWRAM_BSS static Zone s_zones[MAX_ZONES];        // cleared when a scene starts
static u32 s_zone_frame;
static s16 s_zone_scene = -1;

// Round a pixel distance to tiles (half a tile rounds up).
static s32 tiles(s32 px) { return (px + 4) >> 3; }

int moves_tiles(const Actor *a, const Actor *b) {
    s32 dx = fx_abs(fx_to_int(b->x) - fx_to_int(a->x)), dy = fx_abs(fx_to_int(b->y) - fx_to_int(a->y));
    return tiles(dx > dy ? dx : dy);
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
    s32 ux = fx_to_int(user->x), uy = fx_to_int(user->y);
    for (int i = 0; i < MAX_ACTORS && n < MAX_TARGETS; i++) {
        Actor *t = &g_actors[i];
        if (!t->active || !monster_is_foe(user, t)) continue;
        s32 tx = tiles(fx_to_int(t->x) - ux), ty = tiles(fx_to_int(t->y) - uy);
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
        for (int k = 1; k <= m->range; k++) fx(m, ux + user->look_x * 8 * k, uy + user->look_y * 8 * k);
    } else if (m->effect_at == FX_AT_SCREEN) {
        for (int k = 0; k < 5; k++) fx(m, camera_x() + 8, camera_y() + 20 + 32 * k);
    }
}

// The move goes off: sound, attack pose, its actions.
static void begin(Actor *user, const MoveData *m) {
    if (m->sfx) audio_play_sfx(m->sfx);
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

// Can the user's hitbox stand at (x, y)? (not in a wall, not inside a solid monster)
static int free_at(const Actor *u, fixed x, fixed y) {
    s32 l = fx_to_int(x) + u->hb_x, t = fx_to_int(y) + u->hb_y;
    s32 r = l + u->hb_w - 1, b = t + u->hb_h - 1, mx = (l + r) >> 1;
    if ((physics_tile_at(l, t) | physics_tile_at(mx, t) | physics_tile_at(r, t) |
         physics_tile_at(l, b) | physics_tile_at(mx, b) | physics_tile_at(r, b)) & TILE_SOLID) return 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor *o = &g_actors[i];
        if (o == u || !actor_alive(o) || o->solid_mode == SOLID_NONE) continue;
        s32 ol, ot, orr, ob;
        actor_rect(o, &ol, &ot, &orr, &ob);
        if (l <= orr && ol <= r && t <= ob && ot <= b) return 0;
    }
    return 1;
}

// Rush up to `px` pixels toward a point, stopping at walls and monsters.
static void dash(Actor *u, s32 tx, s32 ty, int px) {
    s32 dx = tx - fx_to_int(u->x), dy = ty - fx_to_int(u->y);
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
    if (len == 0) return;
    fixed sx = dx * FX_ONE / len, sy = dy * FX_ONE / len;
    for (int i = 0; i < px; i++) {
        if (!free_at(u, u->x + sx, u->y + sy)) break;
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
        if (m->effect_at == FX_AT_CENTER) fx(m, z->x + z->w / 2, z->y + z->h / 2);
        return;
    }
}

// Where the zones of a move go: the tiles in front of the user, or spots on the screen
// (each one on a monster in view when there is one, else anywhere in view).
static void zones_place(Actor *user, const MoveData *m) {
    s32 ux = fx_to_int(user->x) >> 3, uy = (fx_to_int(user->y) - 4) >> 3;
    if (m->zone_place == ZONE_FRONT) {
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

static void zone_hurt(Zone *z) {
    const MoveData *m = z->move;
    MonsterHit h;
    h.move = m;
    h.team = z->team;
    h.level = z->level;
    h.roll = 0;                     // the ground never misses
    h.power = z->power;
    h.from_x = z->x + z->w / 2;
    h.from_y = z->y + z->h / 2;
    if (m->effect_at == FX_AT_TILES)
        for (int y = 0; y < z->h; y += 8)
            for (int x = 0; x < z->w; x += 8) fx(m, z->x + x + 4, z->y + y + 4);
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *t = &g_actors[i];
        if (!t->active || !monster_targetable(t) || monster_team(t) == z->team) continue;
        s32 l, tp, r, b;
        actor_rect(t, &l, &tp, &r, &b);
        if (l < z->x + z->w && r >= z->x && tp < z->y + z->h && b >= z->y) monster_hit(t, &h);
    }
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
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &s_zones[i];
        if (!z->active) continue;
        if (z->delay) { z->delay--; continue; }
        if (z->clock == 0) {
            zone_hurt(z);
            if (z->move->zone_every == 0) { z->active = 0; continue; }
            z->clock = z->move->zone_every;
        }
        z->clock--;
        if (--z->ticks == 0) z->active = 0;
    }
}

// ---- using a move -----------------------------------------------------------------------
int moves_use(Actor *user, const MoveData *m) {
    if (!m || !monster_can_act(user)) return 0;
    MonsterHit h = hit_from(user, m);
    Actor *t[MAX_TARGETS];
    switch (m->shape) {
    case SHAPE_SINGLE:
    case SHAPE_DASH: {
        Actor *f = nearest_foe(user, moves_reach(m));
        if (!f) return 0;
        begin(user, m);
        if (m->shape == SHAPE_DASH) {
            dash(user, fx_to_int(f->x), fx_to_int(f->y), m->dash * 8);
            h = hit_from(user, m);
            if (moves_tiles(user, f) > m->range) return 1;      // rushed in, but it got away
        }
        t[0] = f;
        fx_targets(user, m, t, 1);
        monster_hit(f, &h);
        return 1;
    }
    case SHAPE_CONE:
    case SHAPE_CIRCLE:
    case SHAPE_SCREEN: {
        int n = area_targets(user, m, t);
        if (!n) return 0;
        begin(user, m);
        fx_area(user, m);
        fx_targets(user, m, t, n);
        for (int k = 0; k < n; k++) monster_hit(t[k], &h);
        return 1;
    }
    case SHAPE_ZONE:
        if (m->zone_place == ZONE_FRONT ? !nearest_foe(user, m->range) : !any_foe_on_screen(user)) return 0;
        begin(user, m);
        fx_area(user, m);
        zones_place(user, m);
        return 1;
    case SHAPE_SELF:
        begin(user, m);
        fx_area(user, m);
        monster_buff(user, m->buff, rng_range(m->ticks_min, m->ticks_max));
        return 1;
    }
    return 0;
}
