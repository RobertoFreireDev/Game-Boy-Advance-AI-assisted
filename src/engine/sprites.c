// sprites.c - OAM shadow buffer and OBJ VRAM allocation.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include <tonc_oam.h>
#include "sprites.h"
#include "config.h"
#include "actor.h"
#include "particles.h"
#include "camera.h"

typedef struct { const SpriteData *sprite; s16 base; } Sheet;

static OBJ_ATTR s_oam[128];
static int s_count;
static Sheet s_sheets[MAX_SPRITE_SHEETS];
static int s_sheet_count;
static int s_next_tile;
static const PaletteData *s_pals[16];
static int s_pal_count;

void sprites_reset(void) {
    s_sheet_count = 0;
    s_next_tile = 0;
    s_pal_count = 0;
    s_count = 0;
    for (int i = 0; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

// First OBJ tile of the sheet; uploads it on first use. -1 if VRAM is full.
static int sheet_base(const SpriteData *s) {
    for (int i = 0; i < s_sheet_count; i++)
        if (s_sheets[i].sprite == s) return s_sheets[i].base;
    int size = s->tiles_per_frame * s->frame_count;
    if (s_sheet_count >= MAX_SPRITE_SHEETS || s_next_tile + size > MAX_OBJ_TILES) return -1;
    int base = s_next_tile;
    memcpy32((u32 *)MEM_VRAM_OBJ + base * 8, s->tiles, size * 8);
    s_sheets[s_sheet_count].sprite = s;
    s_sheets[s_sheet_count].base = (s16)base;
    s_sheet_count++;
    s_next_tile += size;
    return base;
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

void sprites_draw(const SpriteData *s, u16 frame, s32 sx, s32 sy, u8 flip) {
    if (s_count >= 128) return;
    if (sx <= -(s32)s->width || sx >= 240 || sy <= -(s32)s->height || sy >= 160) return;
    int base = sheet_base(s);
    if (base < 0) return;
    OBJ_ATTR *o = &s_oam[s_count++];
    o->attr0 = (u16)((sy & 0xFF) | (s->shape << 14));
    o->attr1 = (u16)((sx & 0x1FF) | (s->size << 14) |
                     ((flip & FLIP_X) ? ATTR1_HFLIP : 0) | ((flip & FLIP_Y) ? ATTR1_VFLIP : 0));
    o->attr2 = (u16)((base + frame * s->tiles_per_frame) | ATTR2_PALBANK(obj_bank(s->palette)) | ATTR2_PRIO(1));
}

static void draw_actor(const Actor *a, s32 cx, s32 cy) {
    if ((a->flags & ACTOR_HIDDEN) || !a->body) return;
    if (a->blink && (a->blink & 4)) return;             // flicker while invincible
    const AnimFrame *f = anim_frame(&a->anim);
    if (!f) return;
    const SpriteData *s = a->anim.anim->sprite;
    u8 flip = f->flip ^ (a->facing_left ? FLIP_X : 0);
    // The origin is mirrored with the art, so flipping keeps the feet in place.
    s32 ox = (flip & FLIP_X) ? s->width - a->body->origin_x : a->body->origin_x;
    s32 oy = (flip & FLIP_Y) ? s->height - a->body->origin_y : a->body->origin_y;
    sprites_draw(s, f->frame, fx_to_int(a->x) - ox - cx, fx_to_int(a->y) - oy - cy, flip);
}

void sprites_build(void) {
    s32 cx = camera_x(), cy = camera_y();
    s_count = 0;
    for (int i = 0; i < MAX_ACTORS; i++)                // players on top
        if (g_actors[i].active && g_actors[i].type == NT_PLAYER) draw_actor(&g_actors[i], cx, cy);
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && g_actors[i].type != NT_PLAYER) draw_actor(&g_actors[i], cx, cy);
    particles_draw(cx, cy);
    for (int i = s_count; i < 128; i++) s_oam[i].attr0 = ATTR0_HIDE;
}

void sprites_vblank(void) {
    oam_copy(oam_mem, s_oam, 128);
}
