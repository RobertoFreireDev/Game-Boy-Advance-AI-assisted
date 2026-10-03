// vars.h - the game's global variables (nodes.json game.variables), addressed by VAR_* id.
// Flags are stored as 0 / 1.
#ifndef ENGINE_VARS_H
#define ENGINE_VARS_H

#include <tonc_types.h>

// Put every variable at its starting value (boot).
void vars_init(void);
// Put every non-persistent variable back to its starting value (a new run). Persistent
// variables (permanent unlocks, banked gold) keep their value.
void vars_reset(void);
// Read a variable (0 for an invalid id).
s32 vars_get(s16 id);
// Write a variable.
void vars_set(s16 id, s32 value);
// Add to a variable.
void vars_add(s16 id, s32 delta);
// Counter that changes whenever any variable changes (the HUD redraws when it does).
u32 vars_version(void);
// Direct access for save/load.
s32 *vars_raw(void);

#endif
