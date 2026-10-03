// input.c - button state: held, pressed this frame, released this frame.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include "input.h"

static u16 s_now, s_prev;

void input_update(void) {
    s_prev = s_now;
    s_now = ~REG_KEYINPUT & KEY_MASK;
}

int input_held(u16 mask)     { return (s_now & mask) != 0; }
int input_pressed(u16 mask)  { return (s_now & ~s_prev & mask) != 0; }
int input_released(u16 mask) { return (~s_now & s_prev & mask) != 0; }

int input_dir_x(void) { return input_held(KEY_RIGHT) - input_held(KEY_LEFT); }
int input_dir_y(void) { return input_held(KEY_DOWN) - input_held(KEY_UP); }
