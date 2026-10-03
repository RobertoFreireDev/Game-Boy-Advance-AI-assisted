// camera.h - which part of the world is on screen: follows a target, stays inside the
// map, and can shake.
#ifndef ENGINE_CAMERA_H
#define ENGINE_CAMERA_H

#include <tonc_types.h>

struct Actor;

// Put the camera at a start position (top-left world pixel) with no target.
void camera_reset(s32 x, s32 y, u8 bounds);
// Follow an actor (NULL = stay still). Jumps straight to it on the next update.
void camera_set_target(struct Actor *a);
// An actor is going away: stop following it.
void camera_forget(struct Actor *a);
// Shake the screen for `ticks` by up to `strength` pixels.
void camera_shake(s32 ticks, s32 strength);
// Move toward the target, clamp to the map, apply shake. Once per frame.
void camera_update(void);
// Camera top-left in world pixels (shake included).
s32 camera_x(void);
s32 camera_y(void);

#endif
