// talk - when the player stands close and presses the button, show a dialog.
#include "talk.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/ui.h"

void bhv_talk_update(Actor *a, const void *params) {
    const Params_talk *p = params;
    Actor *pl = actor_player();
    if (!pl) return;
    s32 al, at, ar, ab, pl_l, pl_t, pl_r, pl_b;
    actor_rect(a, &al, &at, &ar, &ab);
    actor_rect(pl, &pl_l, &pl_t, &pl_r, &pl_b);
    int near = pl_l <= ar + p->range && pl_r >= al - p->range &&
               pl_t <= ab + p->range && pl_b >= at - p->range;
    if (!near) return;
    if (p->face_player) {
        s32 dx = fx_to_int(pl->x - a->x), dy = fx_to_int(pl->y - a->y);
        a->facing_left = dx < 0;
        if (fx_abs(dy) > fx_abs(dx)) a->facing = dy < 0 ? DIR_UP : DIR_DOWN;
        else a->facing = dx < 0 ? DIR_LEFT : DIR_RIGHT;
    }
    if (p->dialog && input_pressed(p->button) && !ui_blocking()) {
        actor_sound(a, SND_TALK);
        ui_show_dialog(p->dialog);
    }
}
