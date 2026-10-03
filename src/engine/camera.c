// camera.c - follow target, clamp to bounds, shake.
#include "camera.h"
#include "actor.h"
#include "bg.h"

#define SCREEN_W 240
#define SCREEN_H 160
#define DEADZONE_X 8        // the target can move this far from center before the camera follows

static s32 s_x, s_y, s_ox, s_oy;
static Actor *s_target;
static u8 s_bounds, s_snap;
static u16 s_shake;
static u8 s_strength;

void camera_reset(s32 x, s32 y, u8 bounds) {
    s_x = x;
    s_y = y;
    s_ox = s_oy = 0;
    s_target = NULL;
    s_bounds = bounds;
    s_shake = 0;
    s_snap = 1;
}

void camera_set_target(Actor *a) {
    s_target = a;
    s_snap = 1;
}

void camera_forget(Actor *a) {
    if (s_target == a) s_target = NULL;
}

void camera_shake(s32 ticks, s32 strength) {
    s_shake = (u16)ticks;
    s_strength = (u8)strength;
}

void camera_update(void) {
    if (s_target && s_target->active) {
        s32 tx = fx_to_int(s_target->x) - SCREEN_W / 2;
        s32 ty = fx_to_int(s_target->y) - SCREEN_H / 2 - 8;
        if (s_snap) {
            s_x = tx;
            s_y = ty;
        } else {
            if (tx > s_x + DEADZONE_X) s_x = tx - DEADZONE_X;
            if (tx < s_x - DEADZONE_X) s_x = tx + DEADZONE_X;
            s32 d = ty - s_y;                         // ease vertically
            s_y += (d + (d > 0 ? 3 : d < 0 ? -3 : 0)) / 4;
        }
    }
    s_snap = 0;
    if (s_bounds == CAM_BOUNDS_MAP) {
        s32 w = bg_world_w(), h = bg_world_h();
        if (s_x > w - SCREEN_W) s_x = w - SCREEN_W;
        if (s_y > h - SCREEN_H) s_y = h - SCREEN_H;
        if (s_x < 0) s_x = 0;
        if (s_y < 0) s_y = 0;
    }
    s_ox = s_oy = 0;
    if (s_shake) {
        s_shake--;
        s_ox = rng_range(-s_strength, s_strength);
        s_oy = rng_range(-s_strength, s_strength);
    }
    bg_set_camera(s_x + s_ox, s_y + s_oy);
}

s32 camera_x(void) { return s_x + s_ox; }
s32 camera_y(void) { return s_y + s_oy; }
