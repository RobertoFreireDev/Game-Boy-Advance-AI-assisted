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
        int vertical = fx_abs(dy) > fx_abs(dx);
        actor_face(a, vertical ? 0 : dx < 0 ? -1 : 1, vertical ? (dy < 0 ? -1 : 1) : 0);
        a->facing_left = dx < 0;
    }
    if (p->dialog && input_pressed(p->button) && !ui_blocking()) {
        actor_sound(a, SND_TALK);
        ui_show_dialog(p->dialog);
    }
}
