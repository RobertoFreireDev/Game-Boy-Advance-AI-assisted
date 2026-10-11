// floattext.h - short words that pop up over things and float away: damage numbers, MISS,
// LV UP!. Drawn as 8x8 sprites with a font node's glyphs, in front of everything but the UI.
#ifndef ENGINE_FLOATTEXT_H
#define ENGINE_FLOATTEXT_H

#include "data.h"

// Forget every floating text and the uploaded glyphs (scene change).
void ftext_reset(void);
// Show a word centered on world x, its bottom at world y, in a font and colors (pal NULL =
// the font's). It rises for ~3/4 of a second, then vanishes. Up to 15 characters.
void ftext_show(s32 x, s32 y, const char *text, const FontData *font, const PaletteData *pal);
// Float and age every text. Once per frame (not while paused).
void ftext_update(void);
// Add the texts to the sprite list (camera at cx, cy).
void ftext_draw(s32 cx, s32 cy);

#endif
