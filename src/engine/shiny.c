// shiny.c - rainbow-shimmering copies of palettes (see shiny.h). A copy is worked out again only
// when the hue changes (every SHINY_STEP ticks), and written into its sprite palette bank in VBlank.
#include <tonc_memmap.h>
#include "shiny.h"
#include "config.h"
#include "core.h"
#include "sprites.h"

#define HUE_TURN   1536     // hue units around the rainbow: 6 sectors of 256
#define KEEP       0xFF     // lead[c]: the color keeps its own (an outline)
#define DARK       6        // brightness (0-31) below which a color keeps its own

typedef struct {
    const PaletteData *base;
    PaletteData pal;        // the copy sprites are drawn with
    u8 lead[16];            // per color: how far its hue runs ahead (brightness x 8 hue units), or KEEP
} Shiny;

EWRAM_BSS static Shiny s_shiny[MAX_SHINY];
static u8 s_count;
static s8 s_hue = -1;       // hue step the copies show (-1 = none yet)

// Brightness of a 15-bit color, 0-31.
static int brightness(u16 c) {
    return (int)(((c & 31) * 77 + ((c >> 5) & 31) * 150 + ((c >> 10) & 31) * 29) >> 8);
}

// Half `base`, half the fully bright rainbow color at hue h (0 = red, 256 = yellow, 512 = green...).
static u16 tint(u16 base, u32 h) {
    if (h >= HUE_TURN) h -= HUE_TURN;
    u32 f = h & 255, up = (31 * f) >> 8, dn = 31 - up, r, g, b;
    switch (h >> 8) {
    case 0:  r = 31; g = up; b = 0;  break;
    case 1:  r = dn; g = 31; b = 0;  break;
    case 2:  r = 0;  g = 31; b = up; break;
    case 3:  r = 0;  g = dn; b = 31; break;
    case 4:  r = up; g = 0;  b = 31; break;
    default: r = 31; g = 0;  b = dn; break;
    }
    r = ((base & 31) + r) >> 1;
    g = (((base >> 5) & 31) + g) >> 1;
    b = (((base >> 10) & 31) + b) >> 1;
    return (u16)(r | (g << 5) | (b << 10));
}

// This hue step's colors of a copy.
static void paint(Shiny *s, int hue) {
    u32 h = (u32)hue * (HUE_TURN / SHINY_HUES);
    for (int c = 1; c < s->pal.count; c++)
        s->pal.colors[c] = s->lead[c] == KEEP ? s->base->colors[c] : tint(s->base->colors[c], h + s->lead[c]);
}

static int hue_now(void) {
    return (int)((core_frame() / SHINY_STEP) & (SHINY_HUES - 1));
}

const PaletteData *shiny_palette(const PaletteData *base) {
    if (!base) return NULL;
    for (int i = 0; i < s_count; i++)
        if (s_shiny[i].base == base) return &s_shiny[i].pal;
    if (s_count >= MAX_SHINY) return NULL;
    Shiny *s = &s_shiny[s_count++];
    s->base = base;
    s->pal.count = base->count;
    s->pal.colors[0] = base->colors[0];
    for (int c = 1; c < 16; c++) {
        int y = c < base->count ? brightness(base->colors[c]) : 0;
        s->lead[c] = (u8)(y < DARK ? KEEP : y * 8);
    }
    paint(s, s_hue >= 0 ? s_hue : hue_now());       // its first draw loads these colors
    return &s->pal;
}

void shiny_vblank(void) {
    if (!s_count) return;
    int hue = hue_now();
    if (hue == s_hue) return;
    s_hue = (s8)hue;
    for (int i = 0; i < s_count; i++) {
        Shiny *s = &s_shiny[i];
        paint(s, hue);
        int bank = sprites_palette_bank(&s->pal);   // not drawn yet: its first draw loads it
        if (bank < 0) continue;
        for (int c = 1; c < s->pal.count; c++) pal_obj_bank[bank][c] = s->pal.colors[c];
    }
}

void shiny_reset(void) {
    s_count = 0;
    s_hue = -1;
}
