// interactor - the player's "use" button. On a press it looks at a small box in front of the
// player (the way it faces) and uses the nearest object with the interact behavior there. A
// used press locks the player for 2 ticks, so behaviors listed after this one (a net swing
// with sword_attack on the same button) don't also react to it.
#include "interactor.h"
#include "interact.h"
#include "behavior_params.h"
#include "engine/input.h"
#include "engine/ui.h"

void bhv_interactor_update(Actor *a, const void *params) {
    const Params_interactor *p = params;
    if (!input_pressed(p->button) || ui_blocking() || a->anim_lock || a->stun) return;
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    s32 cx = (l + r) / 2, cy = (t + b) / 2, half = p->width / 2;
    s32 x0, y0, x1, y1;
    switch (a->facing) {
    case DIR_LEFT:  x0 = l - p->reach; x1 = cx; y0 = cy - half; y1 = cy + half; break;
    case DIR_UP:    y0 = t - p->reach; y1 = cy; x0 = cx - half; x1 = cx + half; break;
    case DIR_DOWN:  y0 = cy; y1 = b + p->reach; x0 = cx - half; x1 = cx + half; break;
    default:        x0 = cx; x1 = r + p->reach; y0 = cy - half; y1 = cy + half; break;
    }
    Actor *o = interact_find(a, x0, y0, x1, y1, cx, cy);
    if (!o) return;
    a->vx = a->vy = 0;
    a->anim_lock = 2;
    actor_play_slot(a, ANIM_IDLE);
    interact_use(o, a);
}
