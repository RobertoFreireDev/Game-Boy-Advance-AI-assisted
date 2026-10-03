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
    int near = pl_l <= ar + p->range && pl_r >= al - p->range && pl_t <= ab && pl_b >= at;
    if (!near) return;
    if (p->face_player) a->facing_left = pl->x < a->x;
    if (p->dialog && input_pressed(p->button) && !ui_blocking()) {
        actor_sound(a, SND_TALK);
        ui_show_dialog(p->dialog);
    }
}
