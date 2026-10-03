// input.h - button state: held, pressed this frame, released this frame.
// Button masks are the KEY_* values from libtonc (KEY_A, KEY_LEFT, KEY_START...).
#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

#include <tonc_types.h>

// Read the buttons. Call once per frame.
void input_update(void);
// True while any button in mask is held down.
int input_held(u16 mask);
// True only on the frame a button in mask went down.
int input_pressed(u16 mask);
// True only on the frame a button in mask was let go.
int input_released(u16 mask);
// -1 / 0 / 1 for the horizontal D-pad.
int input_dir_x(void);
// -1 / 0 / 1 for the vertical D-pad (down = 1).
int input_dir_y(void);

#endif
