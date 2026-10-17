// button_actions - when the button is pressed, run the actions (e.g. START opens a pause menu).
// Ignored while a menu or dialog is open, so it never stacks on top of one. With `once`, only
// the first press counts (a START that leaves the screen can't run twice during the fade).
#include "button_actions.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/ui.h"
#include "engine/actions.h"

enum { ST_PRESSED };

void bhv_button_actions_update(Actor *a, const void *params) {
    const Params_button_actions *p = params;
    u32 *st = bhv_state(a);
    if (p->once && st[ST_PRESSED]) return;
    if (input_pressed(p->button) && !ui_blocking()) {
        st[ST_PRESSED] = 1;
        scripts_start(p->on_press, a);
    }
}
