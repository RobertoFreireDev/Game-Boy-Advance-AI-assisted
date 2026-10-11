// floattext.c - floating words (see floattext.h).
#include <string.h>
#include "floattext.h"
#include "config.h"
#include "sprites.h"

#define FT_LIFE  45             // ticks a text stays
#define FT_LEN   15

typedef struct {
    u8 active, len;
    u16 life;
    s32 x, y;                   // world: center, bottom (fixed point y so it rises smoothly)
    fixed rise;
    const FontData *font;
    const PaletteData *pal;
    char text[FT_LEN + 1];
} FText;

EWRAM_BSS static FText s_texts[MAX_FLOAT_TEXTS];    // cleared on scene load
static const FontData *s_font;  // font whose glyphs are in OBJ VRAM
static int s_base = -1;         // first OBJ tile of those glyphs

void ftext_reset(void) {
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) s_texts[i].active = 0;
    s_font = NULL;
    s_base = -1;
}

void ftext_show(s32 x, s32 y, const char *text, const FontData *font, const PaletteData *pal) {
    if (!text || !font) return;
    FText *t = &s_texts[0];
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {         // a free slot, else the oldest text
        if (!s_texts[i].active) { t = &s_texts[i]; break; }
        if (s_texts[i].life < t->life) t = &s_texts[i];
    }
    int n = 0;
    while (text[n] && n < FT_LEN) { t->text[n] = text[n]; n++; }
    t->text[n] = 0;
    t->len = (u8)n;
    t->active = 1;
    t->life = FT_LIFE;
    t->x = x;
    t->y = y;
    t->rise = 0;
    t->font = font;
    t->pal = pal ? pal : font->palette;
}

void ftext_update(void) {
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {
        FText *t = &s_texts[i];
        if (!t->active) continue;
        if (--t->life == 0) { t->active = 0; continue; }
        // Quick pop up, then a slow drift: 1 px a tick for 8 ticks, then 1/4 px.
        t->rise += t->life > FT_LIFE - 8 ? FX_ONE : FX_ONE / 4;
    }
}

void ftext_draw(s32 cx, s32 cy) {
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {
        const FText *t = &s_texts[i];
        if (!t->active) continue;
        if (t->life < 10 && (t->life & 2)) continue;    // blink out at the end
        if (s_font != t->font) {                        // one font's glyphs per scene
            if (s_font) continue;
            s_font = t->font;
            s_base = sprites_upload(t->font->tiles, t->font->glyph_count);
        }
        if (s_base < 0) continue;
        int bank = sprites_palette(t->pal);
        s32 sx = t->x - t->len * 4 - cx;
        s32 sy = t->y - 8 - fx_to_int(t->rise) - cy;
        for (int k = 0; k < t->len; k++, sx += 8) {
            u8 g = t->font->map[(u8)t->text[k] & 127];
            if (g) sprites_tile(s_base + g - 1, bank, sx, sy);
        }
    }
}
