// sprites.iwram.c - (hot code: ARM in IWRAM) OAM shadow buffer and OBJ VRAM allocation.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include <tonc_oam.h>
#include "sprites.h"
#include "config.h"
#include "actor.h"
#include "particles.h"
#include "camera.h"

// An uploaded sheet, with the numbers every sprite of it needs copied from ROM (slow to read)
// into IWRAM: a horde draws ~100 sprites a frame from a handful of sheets.
typedef struct {
    const SpriteData *sprite;
    s16 base;                   // first OBJ tile
    u16 attr2;                  // palette bank + priority
    u16 shape, size;            // attr0 / attr1 shape and size bits
    u8 w, h, tpf;               // pixels, tiles per frame
} Sheet;

static OBJ_ATTR s_oam[128];
static int s_count;
static Sheet s_sheets[MAX_SPRITE_SHEETS];
static int s_sheet_count;
static int s_next_tile;
static const PaletteData *s_pals[16];
static int s_pal_count;
static const Sheet *s_last;       // sheet of the previous sprite (hordes share a few sheets)

void sprites_reset(void) {
    s_sheet_count = 0;
    s_next_tile = 0;
    s_pal_count = 0;
    s_count = 0;
    s_last = NULL;
    for (int i = 0; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

static int obj_bank(const PaletteData *p) {
    for (int i = 0; i < s_pal_count; i++)
        if (s_pals[i] == p) return i;
    if (s_pal_count >= 16) return 0;
    int b = s_pal_count++;
    s_pals[b] = p;
    for (int c = 1; c < p->count; c++) pal_obj_bank[b][c] = p->colors[c];
    return b;
}

// The sheet's VRAM slot; uploads it (and its palette) on first use. NULL if VRAM is full.
static const Sheet *sheet_of(const SpriteData *s) {
    if (s_last && s_last->sprite == s) return s_last;
    for (int i = 0; i < s_sheet_count; i++)
        if (s_sheets[i].sprite == s) return s_last = &s_sheets[i];
    int size = s->tiles_per_frame * s->frame_count;
    if (s_sheet_count >= MAX_SPRITE_SHEETS || s_next_tile + size > MAX_OBJ_TILES) return NULL;
    Sheet *sh = &s_sheets[s_sheet_count++];
    memcpy32((u32 *)MEM_VRAM_OBJ + s_next_tile * 8, s->tiles, size * 8);
    sh->sprite = s;
    sh->base = (s16)s_next_tile;
    sh->attr2 = (u16)(ATTR2_PALBANK(obj_bank(s->palette)) | ATTR2_PRIO(1));
    sh->shape = (u16)(s->shape << 14);
    sh->size = (u16)(s->size << 14);
    sh->w = (u8)s->width;
    sh->h = (u8)s->height;
    sh->tpf = (u8)s->tiles_per_frame;
    s_next_tile += size;
    return s_last = sh;
}

// Add one hardware sprite of an uploaded sheet (skipped when off screen or OAM is full).
static void put_obj(const Sheet *sh, u16 frame, s32 sx, s32 sy, u8 flip) {
    if (s_count >= 128) return;
    if (sx <= -(s32)sh->w || sx >= 240 || sy <= -(s32)sh->h || sy >= 160) return;
    OBJ_ATTR *o = &s_oam[s_count++];
    o->attr0 = (u16)((sy & 0xFF) | sh->shape);
    o->attr1 = (u16)((sx & 0x1FF) | sh->size |
                     ((flip & FLIP_X) ? ATTR1_HFLIP : 0) | ((flip & FLIP_Y) ? ATTR1_VFLIP : 0));
    o->attr2 = (u16)((sh->base + frame * sh->tpf) | sh->attr2);
}

void sprites_draw(const SpriteData *s, u16 frame, s32 sx, s32 sy, u8 flip) {
    if (s_count >= 128) return;
    if (sx <= -(s32)s->width || sx >= 240 || sy <= -(s32)s->height || sy >= 160) return;
    const Sheet *sh = sheet_of(s);
    if (sh) put_obj(sh, frame, sx, sy, flip);
}

static void draw_actor(const Actor *a, s32 cx, s32 cy) {
    if ((a->flags & ACTOR_HIDDEN) || !a->body) return;
    if (a->blink && (a->blink & 4)) return;             // flicker while invincible
    s32 x = fx_to_int(a->x) - cx, y = fx_to_int(a->y) - cy;
    // Far off screen (sprites are at most 64 px and the origin lies inside them): skip it
    // before reading its animation, as most of a horde's gems and stragglers are out of view.
    if (x <= -64 || x >= 240 + 64 || y <= -64 || y >= 160 + 64) return;
    const AnimFrame *f = anim_frame(&a->anim);
    if (!f) return;
    const Sheet *sh = sheet_of(a->anim.anim->sprite);
    if (!sh) return;
    int mirror = a->facing_left && !actor_slot_vertical(a->anim_slot);
    u8 flip = f->flip ^ (mirror ? FLIP_X : 0);
    // The origin is mirrored with the art, so flipping keeps the feet in place.
    s32 ox = (flip & FLIP_X) ? sh->w - a->body->origin_x : a->body->origin_x;
    s32 oy = (flip & FLIP_Y) ? sh->h - a->body->origin_y : a->body->origin_y;
    put_obj(sh, f->frame, x - ox, y - oy, flip);
}

#if GAME_Y_SORT
// Top-down depth: actors near the screen sorted by their feet (origin y), the lowest one first
// in OAM (the GBA draws lower OAM entries in front). At equal height the player is in front.
static void build_sorted(s32 cx, s32 cy) {
    u8 order[MAX_ACTORS];
    s32 key[MAX_ACTORS];
    int n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &g_actors[i];
        if (!a->active || !a->body || (a->flags & ACTOR_HIDDEN)) continue;
        s32 x = fx_to_int(a->x) - cx, y = fx_to_int(a->y) - cy;
        if (x <= -64 || x >= 240 + 64 || y <= -64 || y >= 160 + 64) continue;
        s32 k = y * 2 + (a->type == NT_PLAYER);
        int j = n++;
        while (j > 0 && key[j - 1] < k) {          // insertion sort, biggest key first
            key[j] = key[j - 1];
            order[j] = order[j - 1];
            j--;
        }
        key[j] = k;
        order[j] = (u8)i;
    }
    for (int k = 0; k < n; k++) draw_actor(&g_actors[order[k]], cx, cy);
}
#endif

void sprites_build(void) {
    s32 cx = camera_x(), cy = camera_y();
    s_count = 0;
#if GAME_Y_SORT
    build_sorted(cx, cy);
    particles_draw(cx, cy);
    for (int i = s_count; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
    return;
#endif
    u8 others[MAX_ACTORS];
    int n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {              // players on top, everyone else after
        const Actor *a = &g_actors[i];
        if (!a->active) continue;
        if (a->type == NT_PLAYER) draw_actor(a, cx, cy);
        else others[n++] = (u8)i;
    }
    for (int k = 0; k < n; k++) draw_actor(&g_actors[others[k]], cx, cy);
    particles_draw(cx, cy);
    for (int i = s_count; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

void sprites_vblank(void) {
    oam_copy(oam_mem, s_oam, 128);
}
