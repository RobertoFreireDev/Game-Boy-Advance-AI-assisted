// topdown_controller - walk in any direction with the D-pad (no gravity).
#include "topdown_controller.h"
#include "behavior_params.h"
#include "engine/input.h"

void bhv_topdown_controller_update(Actor *a, const void *params) {
    const Params_topdown_controller *p = params;
    int dx = input_dir_x(), dy = input_dir_y();
    if (!p->eight_way && dx && dy) dy = 0;
    if (a->anim_lock) return;                   // being knocked back
    a->vx = dx * p->speed;
    a->vy = dy * p->speed;
    if (dx && dy) {                             // diagonal: same speed as straight (x 0.707)
        a->vx = a->vx * 181 / 256;
        a->vy = a->vy * 181 / 256;
    }
    if (dx) a->facing_left = dx < 0;
    actor_play_slot(a, (dx || dy) ? ANIM_WALK : ANIM_IDLE);
}
