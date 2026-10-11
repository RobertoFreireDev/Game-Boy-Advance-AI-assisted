// physics.iwram.c - (hot code: ARM in IWRAM) movement, tile collision, platforms and contacts.
#include "physics.h"
#include "bg.h"

#define CAT_BIT(c) (1u << (c))

static const TilemapData *s_map;

// Built once per physics_update so hordes stay fast: the solid actors (others can stand on /
// bump into them) and every live actor grouped by collision category.
static u8 s_solid[MAX_ACTORS];
static int s_solid_n;
static s16 s_srect[MAX_ACTORS][4];  // hitbox of solid k (left, top, right, bottom), kept current as it moves
static u8 s_bucket[CAT_COUNT][MAX_ACTORS];
static u8 s_bucket_n[CAT_COUNT];
static s16 s_rect[MAX_ACTORS][4];   // hitboxes (left, top, right, bottom) for this frame's contacts
static u8 s_seek[MAX_ACTORS];       // live actors that list other categories (they look for contacts)
static int s_seek_n;
static u8 s_cat_seekers[CAT_COUNT]; // how many seekers look into each category this frame

// Broad phase for big crowds: when many seekers (a volley of shots) look into a big bucket (a
// horde), the bucket is binned once into a grid of 32x32-pixel cells by each hitbox's top-left
// corner, so a seeker only tests the few cells around it instead of the whole horde. The grid is
// folded onto 16x16 slots (it repeats every 512 pixels; the exact rectangle test drops the far
// ones). Built at most once per frame per category, only when it pays off.
#define BP_SHIFT    5
#define BP_DIM      16
#define BP_CELLS    (BP_DIM * BP_DIM)
#define BP_MIN_SIZE 16              // smaller buckets are simply scanned
#define BP_MIN_SEEK 4               // and so are buckets few seekers look into
#define BP_SLOTS    2
typedef struct {
    s8 cat;                         // category binned here this frame, -1 = free
    u8 maxw, maxh;                  // largest hitbox in the bucket (pixels)
    u8 start[BP_CELLS + 1];         // items of cell s: item[start[s] .. start[s + 1])
    u8 item[MAX_ACTORS];
} Grid;
static Grid s_grids[BP_SLOTS];
static int s_grid_next;

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
    // Fully solid actors (crates, rocks, top-down monsters) block sideways. The cached hitboxes
    // reject far ones without touching the actors in slow EWRAM (a field of 25 solid monsters).
    s32 l = fx_to_int(nx) + a->hb_x, r = l + a->hb_w - 1;
    s32 t = fx_to_int(a->y) + a->hb_y, b = t + a->hb_h - 1;
    for (int k = 0; k < s_solid_n; k++) {
        const s16 *sr = s_srect[k];
        if (sr[0] > r || sr[2] < l || sr[1] > b || sr[3] < t) continue;
        Actor *s = &g_actors[s_solid[k]];
        if (!blocks(a, s) || s->solid_mode != SOLID_FULL) continue;
        if (!a->gravity) {
            // Top-down: already inside it before this step (it just became solid again):
            // let it walk out instead of jumping to one side.
            s32 ol = fx_to_int(old_x) + a->hb_x;
            if (ol <= sr[2] && sr[0] <= ol + a->hb_w - 1) continue;
        }
        if (nx > old_x) a->x = fx_from_int(sr[0] - a->hb_x - a->hb_w);
        else a->x = fx_from_int(sr[2] + 1 - a->hb_x);
        a->vx = 0;
        a->hit_wall = 1;
        l = fx_to_int(a->x) + a->hb_x;
        r = l + a->hb_w - 1;
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
        // Platforms and solid actors: land on their top. Top-down bodies (no gravity) are
        // only blocked by fully solid ones, like by a wall, and never ride them.
        for (int k = 0; k < s_solid_n; k++) {
            const s16 *rc = s_srect[k];
            s32 sl = rc[0], st = rc[1], sr = rc[2];
            if (r < sl || l > sr) continue;
            s32 new_b = fx_to_int(ny) + a->hb_y + a->hb_h - 1;
            if (!a->gravity && (old_b >= st || new_b < st)) continue;
            int i = s_solid[k];
            Actor *s = &g_actors[i];
            if (!blocks(a, s)) continue;
            if (!a->gravity) {
                if (s->solid_mode == SOLID_FULL && old_b < st && new_b >= st) {
                    ny = fx_from_int(st - a->hb_y - a->hb_h);
                    a->vy = 0;
                    a->hit_wall = 1;
                }
                continue;
            }
            s32 prev_top = fx_to_int(s->prev_y) + s->hb_y;
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
            const s16 *rc = s_srect[k];
            s32 sl = rc[0], sr = rc[2], sb = rc[3];
            new_t = fx_to_int(ny) + a->hb_y;
            if (!(r >= sl && l <= sr && old_t > sb && new_t <= sb)) continue;
            Actor *s = &g_actors[s_solid[k]];
            if (!blocks(a, s) || s->solid_mode != SOLID_FULL) continue;
            {
                ny = fx_from_int(sb + 1 - a->hb_y);
                a->vy = 0;
                if (!a->gravity) a->hit_wall = 1;
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
    if (!a->vx && !a->vy && !a->gravity && a->riding < 0) {
        a->was_on_ground = a->on_ground;    // resting (a gem, a monster standing still): nothing moves
        a->on_ground = 0;
        a->hit_wall = 0;
        if (a->collides & CAT_BIT(CAT_TILES)) touch_tiles(a);
        return;
    }
    if (s_solid_n == 0 && !a->gravity && a->riding < 0 && !(a->collides & CAT_BIT(CAT_TILES))) {
        // Free mover (a shot, a flying monster, a gem) and nothing solid in the scene: there is
        // nothing to collide with, so just move. Most of a big horde takes this path.
        a->was_on_ground = a->on_ground;
        a->on_ground = 0;
        a->hit_wall = 0;
        a->x += a->vx;
        a->y += a->vy;
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
    if (a->vy || a->gravity || a->riding >= 0) move_y(a);
    if (a->collides & CAT_BIT(CAT_TILES)) touch_tiles(a);
}

// Seeker a (pool index i, hitbox ra) against actor j: fire on_touch both ways if they overlap.
// Returns 0 once a is gone (it stops looking).
static inline int pair(Actor *a, int i, const s16 *ra, u32 abit, int j) {
    const s16 *rb = s_rect[j];
    if (ra[0] > rb[2] || rb[0] > ra[2] || ra[1] > rb[3] || rb[1] > ra[3] || j == i) return 1;
    Actor *b = &g_actors[j];
    if (!actor_alive(b)) return 1;
    if ((b->collides & abit) && j < i) return 1;        // done from b's side
    actor_touch(a, b);
    if (actor_alive(a) && actor_alive(b)) actor_touch(b, a);
    return actor_alive(a);
}

// The grid of category c's bucket, binned on first use this frame.
static const Grid *grid_of(int c) {
    for (int k = 0; k < BP_SLOTS; k++)
        if (s_grids[k].cat == c) return &s_grids[k];
    Grid *g = &s_grids[s_grid_next];
    s_grid_next = (s_grid_next + 1) % BP_SLOTS;
    const u8 *bucket = s_bucket[c];
    int n = s_bucket_n[c], w = 0, h = 0;
    u8 cell[MAX_ACTORS];
    for (int s = 0; s <= BP_CELLS; s++) g->start[s] = 0;
    for (int m = 0; m < n; m++) {                       // count per cell
        const s16 *r = s_rect[bucket[m]];
        cell[m] = (u8)((((r[1] >> BP_SHIFT) & (BP_DIM - 1)) << 4) | ((r[0] >> BP_SHIFT) & (BP_DIM - 1)));
        g->start[cell[m]]++;
        if (r[2] - r[0] + 1 > w) w = r[2] - r[0] + 1;
        if (r[3] - r[1] + 1 > h) h = r[3] - r[1] + 1;
    }
    for (int s = 1; s <= BP_CELLS; s++) g->start[s] += g->start[s - 1];   // cell ends
    for (int m = n - 1; m >= 0; m--) g->item[--g->start[cell[m]]] = bucket[m];   // ends -> starts
    g->cat = (s8)c;
    g->maxw = (u8)(w > 255 ? 255 : w);
    g->maxh = (u8)(h > 255 ? 255 : h);
    return g;
}

void physics_update(void) {
    s_map = bg_collision_map();
    s_solid_n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (!a->active || a->solid_mode == SOLID_NONE) continue;
        s16 *rc = s_srect[s_solid_n];
        rc[0] = (s16)(fx_to_int(a->x) + a->hb_x);
        rc[1] = (s16)(fx_to_int(a->y) + a->hb_y);
        rc[2] = (s16)(rc[0] + a->hb_w - 1);
        rc[3] = (s16)(rc[1] + a->hb_h - 1);
        s_solid[s_solid_n++] = (u8)i;
    }
    // Platforms move first so riders can follow them; each one's cached hitbox follows it.
    for (int k = 0; k < s_solid_n; k++) {
        Actor *a = &g_actors[s_solid[k]];
        step(a);
        s16 *rc = s_srect[k];
        rc[0] = (s16)(fx_to_int(a->x) + a->hb_x);
        rc[1] = (s16)(fx_to_int(a->y) + a->hb_y);
        rc[2] = (s16)(rc[0] + a->hb_w - 1);
        rc[3] = (s16)(rc[1] + a->hb_h - 1);
    }

    // Everyone else moves, and in the same pass (its position is final once it has moved) gets
    // ready for contacts: a pair touches when either one collides with the other's category.
    // Each actor only looks at the categories it lists; a pair where both list each other is
    // handled once (by the lower index). The pass caches every hitbox, fills the category
    // buckets and lists the "seekers" (actors that list other categories), so the pair tests
    // below never walk empty slots or re-read slow EWRAM.
    for (int c = 0; c < CAT_COUNT; c++) s_bucket_n[c] = s_cat_seekers[c] = 0;
    for (int g = 0; g < BP_SLOTS; g++) s_grids[g].cat = -1;
    s_seek_n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (!a->active) continue;
        if (a->solid_mode == SOLID_NONE) step(a);
        if (a->flags & ACTOR_DYING) continue;
        s32 l = fx_to_int(a->x) + a->hb_x, t = fx_to_int(a->y) + a->hb_y;
        s_rect[i][0] = (s16)l; s_rect[i][1] = (s16)t;
        s_rect[i][2] = (s16)(l + a->hb_w - 1); s_rect[i][3] = (s16)(t + a->hb_h - 1);
        if (a->category < CAT_COUNT) s_bucket[a->category][s_bucket_n[a->category]++] = (u8)i;
        u32 want = a->collides & ~CAT_BIT(CAT_TILES);
        if (!want) continue;
        s_seek[s_seek_n++] = (u8)i;
        for (int c = 0; want; c++, want >>= 1) s_cat_seekers[c] += want & 1;
    }
    for (int k = 0; k < s_seek_n; k++) {
        int i = s_seek[k];
        Actor *a = &g_actors[i];
        if (!actor_alive(a)) continue;                  // removed by an earlier touch this frame
        const s16 *ra = s_rect[i];
        u32 want = a->collides & ~CAT_BIT(CAT_TILES);
        u32 abit = CAT_BIT(a->category);
        for (int c = 0; want; c++, want >>= 1) {
            if (!(want & 1)) continue;
            const Grid *g = s_bucket_n[c] >= BP_MIN_SIZE && s_cat_seekers[c] >= BP_MIN_SEEK ? grid_of(c) : NULL;
            if (g) {
                s32 x0 = (ra[0] - g->maxw + 1) >> BP_SHIFT, x1 = ra[2] >> BP_SHIFT;
                s32 y0 = (ra[1] - g->maxh + 1) >> BP_SHIFT, y1 = ra[3] >> BP_SHIFT;
                if (x1 - x0 < BP_DIM && y1 - y0 < BP_DIM) {     // (a giant zone just scans)
                    for (s32 gy = y0; gy <= y1; gy++)
                        for (s32 gx = x0; gx <= x1; gx++) {
                            int cell = ((gy & (BP_DIM - 1)) << 4) | (gx & (BP_DIM - 1));
                            for (int m = g->start[cell]; m < g->start[cell + 1]; m++)
                                if (!pair(a, i, ra, abit, g->item[m])) goto next;
                        }
                    continue;
                }
            }
            const u8 *bucket = s_bucket[c];
            int n = s_bucket_n[c];
            for (int m = 0; m < n; m++)
                if (!pair(a, i, ra, abit, bucket[m])) goto next;
        }
    next:;
    }
}
