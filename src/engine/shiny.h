// shiny.h - rainbow-shimmering copies of palettes (a shiny monster). Every SHINY_STEP ticks a
// copy's colors take the next of SHINY_HUES rainbow hues, blended half and half with the original
// colors; the lighter a color, the further along the rainbow its hue (a sheen), and the darkest
// colors (outlines) keep theirs.
#ifndef ENGINE_SHINY_H
#define ENGINE_SHINY_H

#include "data.h"

#define SHINY_STEP 4        // ticks each hue shows
#define SHINY_HUES 8        // hues around the rainbow (45 degrees apart)

// The shimmering copy of `base` (the same base always gives the same copy) to draw sprites with:
// an actor's pal or a queued sprite's palette. A hit's flash still shows over it. NULL when all
// MAX_SHINY copies are taken.
const PaletteData *shiny_palette(const PaletteData *base);
// VBlank: the copies' colors move on to the next hue (written into their sprite palette banks).
void shiny_vblank(void);
// Forget every copy (scene change).
void shiny_reset(void);

#endif
