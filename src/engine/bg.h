// bg.h - background layers: tile/palette upload, scrolling with parallax, and streaming
// big maps into the 32x32-tile hardware maps as the camera moves.
// VRAM plan: BG0 = UI (charblock 0, screenblock 31); layer n (1-3) = charblock n, screenblock 31-n.
#ifndef ENGINE_BG_H
#define ENGINE_BG_H

#include "data.h"

// Clear every layer, forget palettes, set the backdrop color.
void bg_reset(u16 backdrop);
// Show a tilemap on its layer (1-3) and upload its tiles and palette.
void bg_set_layer(const TilemapData *map);
// The layer-1 map used for collisions (NULL if the scene has none).
const TilemapData *bg_collision_map(void);
// Size in pixels of the scene's world (collision map, else the biggest map, else the screen).
s32 bg_world_w(void);
s32 bg_world_h(void);
// Palette bank (0-15) holding this palette for backgrounds/UI; loads it on first use.
int bg_pal_bank(const PaletteData *pal);
// Tell the layers where the camera is (top-left world pixel).
void bg_set_camera(s32 x, s32 y);
// Draw layer n (1-3) moved by (dx, dy) pixels (a logo dropping in). Takes effect on the next
// camera update; scene changes reset it.
void bg_set_offset(int n, s32 dx, s32 dy);
// VBlank: write scroll registers and draw newly visible map columns/rows.
void bg_vblank(void);
// Redraw the visible part of every layer (after a scene load).
void bg_refresh_all(void);

#endif
