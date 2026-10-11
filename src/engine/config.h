// config.h - pool sizes and limits. The engine never allocates memory: everything is a fixed pool.
// tools/validate.py reads the MAX_* values below to check that scenes fit.
#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#define MAX_ACTORS      96      // actors alive at once (players, enemies, shots, pickups...); lives in EWRAM
#define MAX_LOGIC       10      // behaviors per actor
#define BSTATE_WORDS    5       // private state words each behavior gets per actor
#define MAX_PARTICLES   64      // particles alive at once
#define MAX_EMITTERS    8       // particle emitters placed directly in a scene
#define MAX_SCRIPTS     24      // action lists running at once (waits, fades...)
#define SCRIPT_DEPTH    6       // nested if_var / if_chance levels inside one action list
#define MAX_SPRITE_SHEETS 32    // different sprite sheets in OBJ VRAM at once
#define MAX_OBJ_TILES   1024    // OBJ VRAM tiles (32 KB, 4bpp)
#define MAX_ICONS       24      // different icons in UI VRAM at once
#define MAX_UPGRADE_CHOICES 3   // cards on a level-up (upgrade) menu
#define MAX_QUEUED_SPRITES 24  // extra sprites behaviors draw each frame (portraits, rings, crowns)
#define MAX_FLOAT_TEXTS 8       // floating words at once (damage numbers, MISS, LV UP!)
#define MAX_ZONES       12      // move zones on the ground at once (fire, acid, falling rocks)

#endif
