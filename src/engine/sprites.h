// sprites.h - OAM shadow buffer (the list of hardware sprites) and OBJ VRAM allocation.
#ifndef ENGINE_SPRITES_H
#define ENGINE_SPRITES_H

#include "data.h"

// Forget every uploaded sprite sheet and palette (scene change).
void sprites_reset(void);
// Add one hardware sprite: a frame of a sheet at a screen position. Off-screen ones are skipped.
void sprites_draw(const SpriteData *s, u16 frame, s32 sx, s32 sy, u8 flip);
// Rebuild the sprite list from actors and particles. Once per frame.
void sprites_build(void);
// VBlank: copy the sprite list to the hardware.
void sprites_vblank(void);

#endif
