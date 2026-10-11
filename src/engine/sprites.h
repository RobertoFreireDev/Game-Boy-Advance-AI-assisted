// sprites.h - OAM shadow buffer (the list of hardware sprites) and OBJ VRAM allocation.
#ifndef ENGINE_SPRITES_H
#define ENGINE_SPRITES_H

#include "data.h"

struct Actor;

// Forget every uploaded sprite sheet and palette (scene change).
void sprites_reset(void);
// Add one hardware sprite: a frame of a sheet at a screen position. Off-screen ones are skipped.
void sprites_draw(const SpriteData *s, u16 frame, s32 sx, s32 sy, u8 flip);
// Draw a sprite this frame on top of the actors (until the next frame's logic runs): a frame
// of `s` with its top-left at (x, y) - world pixels, or pixels from `anchor`'s origin when
// anchor is not NULL (it then follows the actor exactly). pal = other colors (NULL = own).
void sprites_queue(const SpriteData *s, u16 frame, const struct Actor *anchor, s32 x, s32 y, u8 flip,
                   const PaletteData *pal);
// Forget the queued sprites (the core does it before each frame's logic).
void sprites_queue_clear(void);
// OBJ palette bank (0-15) holding this palette; loads it on first use.
int sprites_palette(const PaletteData *p);
// Copy raw 4bpp tiles into OBJ VRAM. Returns the first tile number, or -1 if VRAM is full.
int sprites_upload(const u32 *tiles, int count);
// Add one 8x8 hardware sprite of an uploaded tile (floating text glyphs).
void sprites_tile(int tile, int bank, s32 sx, s32 sy);
// Rebuild the sprite list from actors and particles. Once per frame.
void sprites_build(void);
// VBlank: copy the sprite list to the hardware.
void sprites_vblank(void);

#endif
