// physics.iwram.c - (hot code: ARM in IWRAM) movement, tile collision, platforms and contacts.
#include "physics.h"
#include "bg.h"

#define CAT_BIT(c) (1u << (c))

static const TilemapData *s_map;

// Built once per physics_update so hordes stay fast: the solid actors (others can stand on /
// bump into them) and every live actor grouped by collision category.
static u8 s_solid[MAX_ACTORS];
static int s_solid_n;
static u8 s_bucket[CAT_COUNT][MAX_ACTORS];
static u8 s_bucket_n[CAT_COUNT];
static s16 s_rect[MAX_ACTORS][4];   // hitboxes (left, top, right, bottom) for this frame's contacts

static u8 tile_flags(s32 tx, s32 ty) {
    if (!s_map) return 0;
    if (tx < 0 || tx >= s_map->width) return TILE_SOLID;     // invisible walls at the map sides
    if (ty < 0 || ty >= s_map->height) return 0;
    return s_map->tileset->flags[s_map->cells[ty * s_map->width + tx]];
}

u8 physics_tile_at(s32 px, s32 py) {
    s_map = bg_collision_map();
    return tile_flags(px >> 3, py >> 3);
}

int physics_ground_at(s32 px, s32 py) {
    if ((physics_tile_at(px, py) & (TILE_SOLID | TILE_ONE_WAY)) != 0) return 1;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor *s = &g_actors[i];
        if (!s->active || s->solid_mode == SOLID_NONE) continue;
        s32 l, t, r, b;
        actor_rect(s, &l, &t, &r, &b);
        if (px >= l && px <= r && py >= t && py <= b) return 1;
    }
    return 0;
}

// Any tile in column tx between pixel rows top..bottom has one of the flags.
static int column_hits(s32 tx, s32 top, s32 bottom, u8 mask) {
    for (s32 ty = top >> 3; ty <= bottom >> 3; ty++)
        if (tile_flags(tx, ty) & mask) return 1;
    return 0;
}

// Any tile in row ty between pixel columns left..right has one of the flags.
static int row_hits(s32 ty, s32 left, s32 right, u8 mask) {
    for (s32 tx = left >> 3; tx <= right >> 3; tx++)
        if (tile_flags(tx, ty) & mask) return 1;
    return 0;
}

static int blocks(const Actor *a, const Actor *s) {
    return s != a && s->active && s->solid_mode != SOLID_NONE && (a->collides & CAT_BIT(s->category));
}

int physics_overlap(const Actor *a, const Actor *b) {
    s32 al, at, ar, ab, bl, bt, br, bb;
    actor_rect(a, &al, &at, &ar, &ab);
    actor_rect(b, &bl, &bt, &br, &bb);
    return al <= br && bl <= ar && at <= bb && bt <= ab;
}

int physics_is_stomp(const Actor *top, const Actor *bottom) {
    int falling = top->vy > 0 || top->y > top->prev_y;
    s32 prev_feet = fx_to_int(top->prev_y) + top->hb_y + top->hb_h - 1;
    s32 head = fx_to_int(bottom->y) + bottom->hb_y;
    return falling && prev_feet <= head + 4;
}

static void move_x(Actor *a) {
    if (a->vx == 0) return;
    fixed nx = a->x + a->vx;
    if (a->collides & CAT_BIT(CAT_TILES)) {
        s32 top = fx_to_int(a->y) + a->hb_y, bot = top + a->hb_h - 1;
        if (a->vx > 0) {
            s32 right = fx_to_int(nx) + a->hb_x + a->hb_w - 1;
            if (column_hits(right >> 3, top, bot, TILE_SOLID)) {
                nx = fx_from_int(((right >> 3) << 3) - a->hb_x - a->hb_w);
                a->vx = 0;
                a->hit_wall = 1;
            }
        } else {
            s32 left = fx_to_int(nx) + a->hb_x;
            if (column_hits(left >> 3, top, bot, TILE_SOLID)) {
                nx = fx_from_int((((left >> 3) + 1) << 3) - a->hb_x);
                a->vx = 0;
                a->hit_wall = 1;
            }
        }
    }
    fixed old_x = a->x;
    a->x = nx;
    // Fully solid actors (crates, rocks) block sideways.
    for (int k = 0; k < s_solid_n; k++) {
        Actor *s = &g_actors[s_solid[k]];
        if (!blocks(a, s) || s->solid_mode != SOLID_FULL || !physics_overlap(a, s)) continue;
        s32 sl, st, sr, sb;
        actor_rect(s, &sl, &st, &sr, &sb);
        if (nx > old_x) a->x = fx_from_int(sl - a->hb_x - a->hb_w);
        else a->x = fx_from_int(sr + 1 - a->hb_x);
        a->vx = 0;
        a->hit_wall = 1;
    }
}

static void move_y(Actor *a) {
    fixed ny = a->y + a->vy;
    s32 l = fx_to_int(a->x) + a->hb_x, r = l + a->hb_w - 1;
    s32 old_t = fx_to_int(a->y) + a->hb_y;
    s32 old_b = old_t + a->hb_h - 1;
    int tiles = (a->collides & CAT_BIT(CAT_TILES)) != 0;

    if (a->vy >= 0) {
        if (tiles) {
            s32 new_b = fx_to_int(ny) + a->hb_y + a->hb_h - 1;
            // Rows from just below the old feet to just below the new feet.
            for (s32 ty = (old_b + 1) >> 3; ty <= (new_b + 1) >> 3; ty++) {
                s32 row_top = ty << 3;
                int hit = row_hits(ty, l, r, TILE_SOLID) ||
                          (row_top > old_b && !a->climbing && row_hits(ty, l, r, TILE_ONE_WAY));
                if (hit) {
                    ny = fx_from_int(row_top - a->hb_y - a->hb_h);
                    a->vy = 0;
                    a->on_ground = 1;
                    break;
                }
            }
        }
        // Platforms and solid actors: land on their top.
        for (int k = 0; k < s_solid_n; k++) {
            int i = s_solid[k];
            Actor *s = &g_actors[i];
            if (!blocks(a, s)) continue;
            s32 sl, st, sr, sb;
            actor_rect(s, &sl, &st, &sr, &sb);
            if (r < sl || l > sr) continue;
            s32 prev_top = fx_to_int(s->prev_y) + s->hb_y;
            s32 new_b = fx_to_int(ny) + a->hb_y + a->hb_h - 1;
            if (old_b <= (st > prev_top ? st : prev_top) + 1 && new_b + 1 >= st) {
                ny = fx_from_int(st - a->hb_y - a->hb_h);
                a->vy = 0;
                a->on_ground = 1;
                a->riding = (s8)i;
                break;
            }
        }
    } else {
        s32 new_t = fx_to_int(ny) + a->hb_y;
        if (tiles) {
            for (s32 ty = (old_t - 1) >> 3; ty >= new_t >> 3; ty--) {
                if (row_hits(ty, l, r, TILE_SOLID)) {
                    ny = fx_from_int(((ty + 1) << 3) - a->hb_y);
                    a->vy = 0;
                    break;
                }
            }
        }
        for (int k = 0; k < s_solid_n; k++) {       // bump the head on fully solid actors
            Actor *s = &g_actors[s_solid[k]];
            if (!blocks(a, s) || s->solid_mode != SOLID_FULL) continue;
            s32 sl, st, sr, sb;
            actor_rect(s, &sl, &st, &sr, &sb);
            new_t = fx_to_int(ny) + a->hb_y;
            if (r >= sl && l <= sr && old_t > sb && new_t <= sb) {
                ny = fx_from_int(sb + 1 - a->hb_y);
                a->vy = 0;
            }
        }
    }
    a->y = ny;
}

static void touch_tiles(Actor *a) {
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    u8 f = 0;
    for (s32 ty = t >> 3; ty <= b >> 3; ty++)
        for (s32 tx = l >> 3; tx <= r >> 3; tx++)
            f |= tile_flags(tx, ty);
    a->tile_touch = f & (TILE_HAZARD | TILE_LADDER);
    if (s_map && t > s_map->height * 8 + 32) a->flags |= ACTOR_FELL;
}

static void step(Actor *a) {
    a->prev_x = a->x;
    a->prev_y = a->y;
    if (!a->vx && !a->vy && !a->gravity && a->riding < 0 && !(a->collides & CAT_BIT(CAT_TILES))) {
        a->was_on_ground = a->on_ground;    // resting (a gem on the ground): nothing to move
        a->on_ground = 0;
        a->hit_wall = 0;
        return;
    }
    // Ride along with the platform we stood on last frame (it already moved).
    if (a->riding >= 0) {
        Actor *s = &g_actors[(int)a->riding];
        if (s->active) {
            a->x += s->x - s->prev_x;
            a->y += s->y - s->prev_y;
        }
        a->riding = -1;
    }
    a->was_on_ground = a->on_ground;
    a->on_ground = 0;
    a->hit_wall = 0;
    if (a->gravity) {
        a->vy += g_game.gravity;
        if (a->vy > g_game.max_fall) a->vy = g_game.max_fall;
    }
    move_x(a);
    move_y(a);
    if (a->collides & CAT_BIT(CAT_TILES)) touch_tiles(a);
}

void physics_update(void) {
    s_map = bg_collision_map();
    s_solid_n = 0;
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && g_actors[i].solid_mode != SOLID_NONE) s_solid[s_solid_n++] = (u8)i;
    // Platforms move first so riders can follow them.
    for (int k = 0; k < s_solid_n; k++) step(&g_actors[s_solid[k]]);
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && g_actors[i].solid_mode == SOLID_NONE) step(&g_actors[i]);

    // Contacts: a pair touches when either one collides with the other's category.
    // Each actor only looks at the categories it lists; a pair where both list each other
    // is handled once (by the lower index).
    for (int c = 0; c < CAT_COUNT; c++) s_bucket_n[c] = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (!actor_alive(a) || a->category >= CAT_COUNT) continue;
        s_bucket[a->category][s_bucket_n[a->category]++] = (u8)i;
        s32 l, t, r, b;
        actor_rect(a, &l, &t, &r, &b);
        s_rect[i][0] = (s16)l; s_rect[i][1] = (s16)t; s_rect[i][2] = (s16)r; s_rect[i][3] = (s16)b;
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (!actor_alive(a) || !(a->collides & ~CAT_BIT(CAT_TILES))) continue;
        const s16 *ra = s_rect[i];
        for (int c = 0; c < CAT_COUNT && actor_alive(a); c++) {
            if (!(a->collides & CAT_BIT(c))) continue;
            for (int k = 0; k < s_bucket_n[c]; k++) {
                int j = s_bucket[c][k];
                const s16 *rb = s_rect[j];
                if (ra[0] > rb[2] || rb[0] > ra[2] || ra[1] > rb[3] || rb[1] > ra[3] || j == i) continue;
                Actor *b = &g_actors[j];
                if (!actor_alive(b)) continue;
                if ((b->collides & CAT_BIT(a->category)) && j < i) continue;     // done from b's side
                actor_touch(a, b);
                if (actor_alive(a) && actor_alive(b)) actor_touch(b, a);
                if (!actor_alive(a)) break;
            }
        }
    }
}
