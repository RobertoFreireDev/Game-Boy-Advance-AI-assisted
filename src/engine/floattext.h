// floattext.h - short words that pop up over things and float away: damage numbers, MISS,
// LV UP!; and words pinned over an actor (a monster's level tag). Drawn as 8x8 sprites with a
// font node's glyphs, in front of everything but the UI.
#ifndef ENGINE_FLOATTEXT_H
#define ENGINE_FLOATTEXT_H

#include "data.h"

struct Actor;

// Forget every floating text and the uploaded glyphs (scene change).
void ftext_reset(void);
// Show a word centered on world x, its bottom at world y, in a font and colors (pal NULL =
// the font's). It rises for ~3/4 of a second, then vanishes. Up to 15 characters.
void ftext_show(s32 x, s32 y, const char *text, const FontData *font, const PaletteData *pal);
// Float and age every text. Once per frame (not while paused).
void ftext_update(void);
// Pin a word over an actor this frame (until the next frame's logic runs): centered on its
// origin x, its bottom `dy` pixels from its origin y; it follows the actor exactly. Up to 7
// characters (pal NULL = the font's colors).
void ftext_pin(const struct Actor *anchor, s32 dy, const char *text, const FontData *font,
               const PaletteData *pal);
// Take an actor's pinned words away at once (it was hit: they would cover the damage numbers).
void ftext_unpin(const struct Actor *anchor);
// Forget the pinned words (the scene does it before each frame's logic).
void ftext_pins_clear(void);
// Add the texts and pinned words to the sprite list (camera at cx, cy).
void ftext_draw(s32 cx, s32 cy);

#endif
