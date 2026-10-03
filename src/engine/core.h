// core.h - boot and the main loop (one pass per frame, ~60 per second).
#ifndef ENGINE_CORE_H
#define ENGINE_CORE_H

#include <tonc_types.h>

// Set up interrupts, sound and variables, and request the start scene.
void core_init(void);
// Run the game forever. Each frame follows the order in CLAUDE.md §8.
void core_run(void);
// Frames since boot.
u32 core_frame(void);
// True while a menu or dialog pauses the world (actors, physics, animations).
int core_paused(void);

#endif
