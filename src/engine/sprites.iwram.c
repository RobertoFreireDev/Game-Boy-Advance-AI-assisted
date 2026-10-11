// sprites.iwram.c - (hot code: ARM in IWRAM) OAM shadow buffer and OBJ VRAM allocation.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include <tonc_oam.h>
#include "sprites.h"
#include "config.h"
#include "actor.h"
#include "particles.h"
#include "floattext.h"
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

// A sprite a behavior asked for this frame (a portrait, a charge ring, a crown...).
typedef struct {
    const SpriteData *sprite;
    const PaletteData *pal;     // NULL = the sprite's own colors
    const Actor *anchor;        // x, y are relative to its origin (NULL = world position)
    s16 x, y;                   // top-left
    u16 frame;
    u8 flip, pad;
} Queued;

static OBJ_ATTR s_oam[128];
static int s_count;
static Sheet s_sheets[MAX_SPRITE_SHEETS];
static int s_sheet_count;
static int s_next_tile;
static const PaletteData *s_pals[16];
static int s_pal_count;
static const Sheet *s_last;       // sheet of the previous sprite (hordes share a few sheets)
EWRAM_BSS static Queued s_queue[MAX_QUEUED_SPRITES];     // (EWRAM: a few a frame)
static int s_queued;

void sprites_reset(void) {
    s_sheet_count = 0;
    s_next_tile = 0;
    s_pal_count = 0;
    s_count = 0;
    s_last = NULL;
    s_queued = 0;
    for (int i = 0; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

int sprites_palette(const PaletteData *p) {
    for (int i = 0; i < s_pal_count; i++)
        if (s_pals[i] == p) return i;
    if (s_pal_count >= 16) return 0;
    int b = s_pal_count++;
    s_pals[b] = p;
    for (int c = 1; c < p->count; c++) pal_obj_bank[b][c] = p->colors[c];
    return b;
}

int sprites_upload(const u32 *tiles, int count) {
    if (s_next_tile + count > MAX_OBJ_TILES) return -1;
    int base = s_next_tile;
    memcpy32((u32 *)MEM_VRAM_OBJ + base * 8, tiles, count * 8);
    s_next_tile += count;
    return base;
}

// The sheet's VRAM slot; uploads it (and its palette) on first use. NULL if VRAM is full.
static const Sheet *sheet_of(const SpriteData *s) {
    if (s_last && s_last->sprite == s) return s_last;
    for (int i = 0; i < s_sheet_count; i++)
        if (s_sheets[i].sprite == s) return s_last = &s_sheets[i];
    int size = s->tiles_per_frame * s->frame_count;
    if (s_sheet_count >= MAX_SPRITE_SHEETS || s_next_tile + size > MAX_OBJ_TILES) return NULL;
    Sheet *sh = &s_sheets[s_sheet_count++];
    sh->sprite = s;
    sh->base = (s16)sprites_upload(s->tiles, size);
    sh->attr2 = (u16)(ATTR2_PALBANK(sprites_palette(s->palette)) | ATTR2_PRIO(1));
    sh->shape = (u16)(s->shape << 14);
    sh->size = (u16)(s->size << 14);
    sh->w = (u8)s->width;
    sh->h = (u8)s->height;
    sh->tpf = (u8)s->tiles_per_frame;
    return s_last = sh;
}

// The attr2 bits for drawing a sheet in other colors (NULL = its own).
static u16 colors_of(const Sheet *sh, const PaletteData *pal) {
    if (!pal) return sh->attr2;
    return (u16)(ATTR2_PALBANK(sprites_palette(pal)) | ATTR2_PRIO(1));
}

// Add one hardware sprite of an uploaded sheet (skipped when off screen or OAM is full).
static void put_obj(const Sheet *sh, u16 frame, s32 sx, s32 sy, u8 flip, u16 attr2) {
    if (s_count >= 128) return;
    if (sx <= -(s32)sh->w || sx >= 240 || sy <= -(s32)sh->h || sy >= 160) return;
    OBJ_ATTR *o = &s_oam[s_count++];
    o->attr0 = (u16)((sy & 0xFF) | sh->shape);
    o->attr1 = (u16)((sx & 0x1FF) | sh->size |
                     ((flip & FLIP_X) ? ATTR1_HFLIP : 0) | ((flip & FLIP_Y) ? ATTR1_VFLIP : 0));
    o->attr2 = (u16)((sh->base + frame * sh->tpf) | attr2);
}

void sprites_draw(const SpriteData *s, u16 frame, s32 sx, s32 sy, u8 flip) {
    if (s_count >= 128) return;
    if (sx <= -(s32)s->width || sx >= 240 || sy <= -(s32)s->height || sy >= 160) return;
    const Sheet *sh = sheet_of(s);
    if (sh) put_obj(sh, frame, sx, sy, flip, sh->attr2);
}

void sprites_tile(int tile, int bank, s32 sx, s32 sy) {
    if (s_count >= 128 || tile < 0) return;
    if (sx <= -8 || sx >= 240 || sy <= -8 || sy >= 160) return;
    OBJ_ATTR *o = &s_oam[s_count++];
    o->attr0 = (u16)(sy & 0xFF);                        // square
    o->attr1 = (u16)(sx & 0x1FF);                       // 8x8
    o->attr2 = (u16)(tile | ATTR2_PALBANK(bank) | ATTR2_PRIO(1));
}

void sprites_queue(const SpriteData *s, u16 frame, const Actor *anchor, s32 x, s32 y, u8 flip,
                   const PaletteData *pal) {
    if (s_queued >= MAX_QUEUED_SPRITES || !s) return;
    Queued *q = &s_queue[s_queued++];
    q->sprite = s;
    q->pal = pal;
    q->anchor = anchor;
    q->x = (s16)x;
    q->y = (s16)y;
    q->frame = frame;
    q->flip = flip;
}

void sprites_queue_clear(void) { s_queued = 0; }

static void draw_queued(s32 cx, s32 cy) {
    for (int i = 0; i < s_queued; i++) {
        const Queued *q = &s_queue[i];
        s32 x = q->x - cx, y = q->y - cy;
        if (q->anchor) {
            if (!q->anchor->active) continue;
            x += fx_to_int(q->anchor->x);
            y += fx_to_int(q->anchor->y);
        }
        const Sheet *sh = sheet_of(q->sprite);
        if (sh) put_obj(sh, q->frame, x, y, q->flip, colors_of(sh, q->pal));
    }
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
    u16 attr2 = sh->attr2;
    if (a->flash && a->flash_pal) attr2 = colors_of(sh, a->flash_pal);
    else if (a->pal) attr2 = colors_of(sh, a->pal);
    put_obj(sh, f->frame, x - ox, y - oy, flip, attr2);
}

void sprites_build(void) {
    s32 cx = camera_x(), cy = camera_y();
    s_count = 0;
    // Front to back: floating text, queued sprites, attack effects, players, everyone else,
    // other particles (dust, sparks) behind them.
    ftext_draw(cx, cy);
    draw_queued(cx, cy);
    particles_draw(cx, cy, 1);
    u8 others[MAX_ACTORS];
    int n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &g_actors[i];
        if (!a->active) continue;
        if (a->type == NT_PLAYER) draw_actor(a, cx, cy);
        else others[n++] = (u8)i;
    }
    for (int k = 0; k < n; k++) draw_actor(&g_actors[others[k]], cx, cy);
    particles_draw(cx, cy, 0);
    for (int i = s_count; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

void sprites_vblank(void) {
    oam_copy(oam_mem, s_oam, 128);
}
