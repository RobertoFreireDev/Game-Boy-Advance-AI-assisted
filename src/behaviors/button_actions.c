// button_actions - when the button is pressed, run the actions (e.g. START opens a pause menu).
// Ignored while a menu or dialog is open, so it never stacks on top of one.
#include "button_actions.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/ui.h"
#include "engine/actions.h"

void bhv_button_actions_update(Actor *a, const void *params) {
    const Params_button_actions *p = params;
    if (input_pressed(p->button) && !ui_blocking()) scripts_start(p->on_press, a);
}
