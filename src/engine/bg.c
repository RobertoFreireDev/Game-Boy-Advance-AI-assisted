// bg.c - background layers, palettes, scrolling and map streaming.
#include <string.h>
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include "bg.h"

#define VIEW_COLS 31        // tiles visible across (240 px + partial tile)
#define VIEW_ROWS 21        // tiles visible down (160 px + partial tile)

typedef struct {
    const TilemapData *map;
    u8 bank, valid;
    s32 sx, sy;             // scroll in pixels
    s32 tx, ty;             // top-left tile already drawn
} Layer;

static Layer s_layers[4];   // 1..3 used
static const PaletteData *s_banks[16];
static int s_bank_count;

static volatile u16 *bg_cnt(int n) { return (volatile u16 *)(REG_BASE + 0x0008 + n * 2); }
static volatile u16 *bg_hofs(int n) { return (volatile u16 *)(REG_BASE + 0x0010 + n * 4); }
static volatile u16 *bg_vofs(int n) { return (volatile u16 *)(REG_BASE + 0x0012 + n * 4); }

static void update_dispcnt(void) {
    u32 d = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D | DCNT_BG0;
    for (int n = 1; n <= 3; n++)
        if (s_layers[n].map) d |= DCNT_BG0 << n;
    REG_DISPCNT = d;
}

void bg_reset(u16 backdrop) {
    memset(s_layers, 0, sizeof(s_layers));
    s_bank_count = 0;
    for (int n = 1; n <= 3; n++) {
        memset32(&se_mem[31 - n][0], 0, 512);
        *bg_hofs(n) = 0;
        *bg_vofs(n) = 0;
    }
    pal_bg_mem[0] = backdrop;
    update_dispcnt();
}

int bg_pal_bank(const PaletteData *pal) {
    for (int i = 0; i < s_bank_count; i++)
        if (s_banks[i] == pal) return i;
    if (s_bank_count >= 16) return 0;
    int b = s_bank_count++;
    s_banks[b] = pal;
    for (int c = 1; c < pal->count; c++) pal_bg_bank[b][c] = pal->colors[c];   // index 0 stays transparent
    return b;
}

void bg_set_layer(const TilemapData *map) {
    int n = map->layer;
    if (n < 1 || n > 3) return;
    Layer *L = &s_layers[n];
    L->map = map;
    L->bank = (u8)bg_pal_bank(map->tileset->palette);
    L->valid = 0;
    memset32(&tile_mem[n][0], 0, 8);                                    // tile 0 = empty
    memcpy32(&tile_mem[n][1], &map->tileset->tiles[8], (map->tileset->tile_count - 1) * 8);
    *bg_cnt(n) = BG_CBB(n) | BG_SBB(31 - n) | BG_4BPP | BG_REG_32x32 | BG_PRIO(n);
    update_dispcnt();
}

const TilemapData *bg_collision_map(void) { return s_layers[1].map; }

s32 bg_world_w(void) {
    if (s_layers[1].map) return s_layers[1].map->width * 8;
    s32 w = 240;
    for (int n = 2; n <= 3; n++)
        if (s_layers[n].map && s_layers[n].map->width * 8 > w) w = s_layers[n].map->width * 8;
    return w;
}

s32 bg_world_h(void) {
    if (s_layers[1].map) return s_layers[1].map->height * 8;
    s32 h = 160;
    for (int n = 2; n <= 3; n++)
        if (s_layers[n].map && s_layers[n].map->height * 8 > h) h = s_layers[n].map->height * 8;
    return h;
}

void bg_set_camera(s32 x, s32 y) {
    for (int n = 1; n <= 3; n++) {
        Layer *L = &s_layers[n];
        if (!L->map) continue;
        L->sx = (x * L->map->parallax) >> FX_SHIFT;     // parallax 1.0 = moves with the camera
        L->sy = (y * L->map->parallax) >> FX_SHIFT;
    }
}

static u16 entry_at(const Layer *L, s32 tx, s32 ty) {
    const TilemapData *m = L->map;
    if (ty < 0 || ty >= m->height) return 0;
    if (m->repeat_x) {
        tx %= m->width;
        if (tx < 0) tx += m->width;
    } else if (tx < 0 || tx >= m->width) {
        return 0;
    }
    u8 cell = m->cells[ty * m->width + tx];
    return cell ? (u16)(cell | (L->bank << 12)) : 0;
}

static void put(int n, const Layer *L, s32 tx, s32 ty) {
    se_mem[31 - n][(ty & 31) * 32 + (tx & 31)] = entry_at(L, tx, ty);
}

static void draw_column(int n, const Layer *L, s32 tx, s32 ty0) {
    for (int i = 0; i < VIEW_ROWS; i++) put(n, L, tx, ty0 + i);
}

static void draw_row(int n, const Layer *L, s32 ty, s32 tx0) {
    for (int i = 0; i < VIEW_COLS; i++) put(n, L, tx0 + i, ty);
}

static void stream(int n) {
    Layer *L = &s_layers[n];
    s32 ntx = L->sx >> 3, nty = L->sy >> 3;
    s32 dx = ntx - L->tx, dy = nty - L->ty;
    if (!L->valid || dx > 4 || dx < -4 || dy > 4 || dy < -4) {
        for (int i = 0; i < VIEW_ROWS; i++) draw_row(n, L, nty + i, ntx);
    } else {
        while (L->tx < ntx) { L->tx++; draw_column(n, L, L->tx + VIEW_COLS - 1, nty); }
        while (L->tx > ntx) { L->tx--; draw_column(n, L, L->tx, nty); }
        while (L->ty < nty) { L->ty++; draw_row(n, L, L->ty + VIEW_ROWS - 1, ntx); }
        while (L->ty > nty) { L->ty--; draw_row(n, L, L->ty, ntx); }
    }
    L->tx = ntx;
    L->ty = nty;
    L->valid = 1;
}

void bg_vblank(void) {
    for (int n = 1; n <= 3; n++) {
        Layer *L = &s_layers[n];
        if (!L->map) continue;
        stream(n);
        *bg_hofs(n) = (u16)L->sx;
        *bg_vofs(n) = (u16)L->sy;
    }
}

void bg_refresh_all(void) {
    for (int n = 1; n <= 3; n++) {
        if (!s_layers[n].map) continue;
        s_layers[n].valid = 0;
        stream(n);
    }
}
