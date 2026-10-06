// bg.c - background layers, palettes, scrolling and map streaming.
#include <string.h>
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include "bg.h"
#include "config.h"

#define VIEW_COLS 31        // tiles visible across (240 px + partial tile)
#define VIEW_ROWS 21        // tiles visible down (160 px + partial tile)

typedef struct {
    const TilemapData *map;
    u8 bank, valid;
    s32 sx, sy;             // scroll in pixels
    s32 tx, ty;             // top-left tile already drawn
    u8 anim_frame[MAX_TILE_ANIMS];  // frame each tile animation is on
    u8 anim_tick[MAX_TILE_ANIMS];   // ticks spent on that frame
    u8 anim_shown[MAX_TILE_ANIMS];  // frame whose pixels are in VRAM (0xFF = none yet)
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
    memset(L->anim_frame, 0, sizeof(L->anim_frame));
    memset(L->anim_tick, 0, sizeof(L->anim_tick));
    memset(L->anim_shown, 0xFF, sizeof(L->anim_shown));
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

// Map rows/columns are copied into the 32x32 hardware map with a pointer per row, not a
// bounds-checked lookup per tile: a full redraw (scene load) is 651 tiles per layer, and every
// tile step of the camera streams a row or a column during VBlank.

// Column tx of the map (wrapped if the map repeats sideways), or -1 if it's off the map.
static s32 map_column(const TilemapData *m, s32 tx) {
    if (m->repeat_x) {
        tx %= m->width;
        return tx < 0 ? tx + m->width : tx;
    }
    return (tx < 0 || tx >= m->width) ? -1 : tx;
}

// One row of the visible window: map row ty, VIEW_COLS tiles from column tx0.
static void draw_row(int n, const Layer *L, s32 ty, s32 tx0) {
    const TilemapData *m = L->map;
    u16 *dst = &se_mem[31 - n][(ty & 31) * 32];
    if (ty < 0 || ty >= m->height) {
        for (int i = 0; i < VIEW_COLS; i++) dst[(tx0 + i) & 31] = 0;
        return;
    }
    const u8 *row = &m->cells[ty * m->width];
    u16 bank = (u16)(L->bank << 12);
    s32 w = m->width, tx = m->repeat_x ? map_column(m, tx0) : tx0;
    for (int i = 0; i < VIEW_COLS; i++, tx0++, tx++) {
        if (m->repeat_x && tx >= w) tx -= w;
        u8 cell = (tx >= 0 && tx < w) ? row[tx] : 0;
        dst[tx0 & 31] = cell ? (u16)(cell | bank) : 0;
    }
}

// One column of the visible window: map column tx, VIEW_ROWS tiles from row ty0.
static void draw_column(int n, const Layer *L, s32 tx, s32 ty0) {
    const TilemapData *m = L->map;
    u16 *dst = &se_mem[31 - n][tx & 31];
    s32 col = map_column(m, tx);
    u16 bank = (u16)(L->bank << 12);
    for (int i = 0; i < VIEW_ROWS; i++, ty0++) {
        u8 cell = (col >= 0 && ty0 >= 0 && ty0 < m->height) ? m->cells[ty0 * m->width + col] : 0;
        dst[(ty0 & 31) * 32] = cell ? (u16)(cell | bank) : 0;
    }
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

// Tile animations swap tile pixels in VRAM, so every copy of a tile on the map changes at
// once and the cost doesn't grow with the map. Pixels are only copied while a chunk of the
// map that uses the animation is on screen; the clock keeps running off screen, so the
// animation picks up in step when it comes back into view.

// True if a 64x64-px chunk marked in `bits` overlaps the layer's view.
static int anim_on_screen(const Layer *L, const u8 *bits) {
    const TilemapData *m = L->map;
    if (m->repeat_x) return 1;                          // wrapping maps: assume visible
    s32 cw = (m->width + 7) >> 3, chh = (m->height + 7) >> 3;
    s32 cx0 = L->sx >> ANIM_CHUNK_SHIFT, cx1 = (L->sx + 239) >> ANIM_CHUNK_SHIFT;
    s32 cy0 = L->sy >> ANIM_CHUNK_SHIFT, cy1 = (L->sy + 159) >> ANIM_CHUNK_SHIFT;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= cw) cx1 = cw - 1;
    if (cy1 >= chh) cy1 = chh - 1;
    for (s32 cy = cy0; cy <= cy1; cy++)
        for (s32 cx = cx0; cx <= cx1; cx++) {
            s32 bit = cy * cw + cx;
            if (bits[bit >> 3] & (1 << (bit & 7))) return 1;
        }
    return 0;
}

// Advance layer n's tile animations one tick and copy changed frames that are on screen.
static void animate_tiles(int n, Layer *L) {
    const TilemapData *m = L->map;
    const TilesetData *ts = m->tileset;
    if (!ts->anim_count || !m->anim_chunks) return;
    const u8 *bits = m->anim_chunks;
    s32 bytes = (((m->width + 7) >> 3) * ((m->height + 7) >> 3) + 7) >> 3;
    for (int g = 0; g < ts->anim_count; g++, bits += bytes) {
        const TileAnimData *a = &ts->anims[g];
        if (++L->anim_tick[g] >= a->ticks[L->anim_frame[g]]) {
            L->anim_tick[g] = 0;
            if (++L->anim_frame[g] >= a->frame_count) L->anim_frame[g] = 0;
        }
        u8 f = L->anim_frame[g];
        if (L->anim_shown[g] == f || !anim_on_screen(L, bits)) continue;
        L->anim_shown[g] = f;
        const u32 *src = &a->frames[f * a->tile_count * 8];
        for (int t = 0; t < a->tile_count; t++, src += 8) memcpy32(&tile_mem[n][a->tiles[t]], src, 8);
    }
}

void bg_vblank(void) {
    for (int n = 1; n <= 3; n++) {
        Layer *L = &s_layers[n];
        if (!L->map) continue;
        stream(n);
        animate_tiles(n, L);
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
