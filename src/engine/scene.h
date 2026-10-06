// scene.h - load / unload scenes, spawn their instances, screen fades.
#ifndef ENGINE_SCENE_H
#define ENGINE_SCENE_H

#include "data.h"

// Ask to switch to a scene. The switch happens at the start of the next scene_update.
void scene_goto(s16 node);
// Per frame: do a pending switch, run action lists, run actor logic (unless paused).
void scene_update(void);
// VBlank: write the fade level to the hardware.
void scene_vblank(void);
// The scene being played (NULL before the first one loads).
const SceneData *scene_current(void);
// Node id of the scene being played.
s16 scene_current_node(void);
// Darken the world (maps + sprites, never the UI): 0 = normal .. 16 = black. Reset per scene.
void scene_set_darkness(int level);
// Fade to black (to_black = 1) or back from black (0) over `ticks`.
void scene_fade(int to_black, int ticks);
// Spawn an object or particle burst at a world position. Returns the actor or NULL.
struct Actor *scene_spawn(s16 node, s32 x, s32 y);

#endif
