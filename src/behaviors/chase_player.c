// chase_player - move toward the player while the player is within range.
#include "chase_player.h"
#include "behavior_params.h"

void bhv_chase_player_update(Actor *a, const void *params) {
    const Params_chase_player *p = params;
    Actor *pl = actor_player();
    if (a->anim_lock) return;
    s32 dx = 0, dy = 0;
    if (a->stun) pl = NULL;                     // stunned: hold still like with no player
    if (pl) {
        dx = fx_to_int(pl->x) - fx_to_int(a->x);
        dy = fx_to_int(pl->y) - fx_to_int(a->y);
    }
    if (!pl || fx_abs(dx) > p->range || fx_abs(dy) > p->range) {
        a->vx = 0;
        if (p->vertical) a->vy = 0;
        actor_play_slot(a, ANIM_IDLE);
        return;
    }
    a->vx = fx_abs(dx) > 2 ? fx_sign(dx) * p->speed : 0;
    if (p->vertical) a->vy = fx_abs(dy) > 2 ? fx_sign(dy) * p->speed : 0;
    if (p->vertical) actor_face(a, fx_abs(dx) >= fx_abs(dy) ? fx_sign(dx) : 0, fx_abs(dy) > fx_abs(dx) ? fx_sign(dy) : 0);
    else if (dx) a->facing_left = dx < 0;
    actor_play_slot(a, ANIM_RUN);
}
