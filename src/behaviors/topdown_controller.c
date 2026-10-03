// topdown_controller - walk in any direction with the D-pad (no gravity).
#include "topdown_controller.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/vars.h"

void bhv_topdown_controller_update(Actor *a, const void *params) {
    const Params_topdown_controller *p = params;
    int dx = input_dir_x(), dy = input_dir_y();
    if (!p->eight_way && dx && dy) dy = 0;
    if (a->anim_lock) return;                   // being knocked back
    fixed speed = p->speed;
    if (p->speed_var >= 0) speed = speed * (10 + vars_get(p->speed_var)) / 10;   // +10% per point
    a->vx = dx * speed;
    a->vy = dy * speed;
    if (dx && dy) {                             // diagonal: same speed as straight (x 0.707)
        a->vx = a->vx * 181 / 256;
        a->vy = a->vy * 181 / 256;
    }
    actor_face(a, dx, dy);                      // picks the up / down / side animations
    actor_play_slot(a, (dx || dy) ? ANIM_WALK : ANIM_IDLE);
}
