// interact - the player can use this object with the A button: talk to a neighbor, shake a
// tree, dig a fossil, read a sign. The player's interactor behavior picks the one in front and
// runs its on_press actions (with this object as 'self'). Tool checks go in the actions
// (if_var on the tool variable).
#include "interact.h"
#include "behavior_params.h"
#include "engine/actions.h"

Actor *interact_find(const Actor *user, s32 l, s32 t, s32 r, s32 b, s32 cx, s32 cy) {
    Actor *best = NULL;
    s32 best_d = 0x7FFFFFFF;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (o == user || !actor_alive(o)) continue;
        int k = actor_logic_index(o, BHV_INTERACT);
        if (k < 0) continue;
        const Params_interact *p = o->logic[k].params;
        s32 ol, ot, orr, ob;
        actor_rect(o, &ol, &ot, &orr, &ob);
        if (orr < ol) orr = ol;                          // a point-sized object still has a spot
        if (ob < ot) ob = ot;
        if (ol - p->range > r || orr + p->range < l || ot - p->range > b || ob + p->range < t) continue;
        s32 d = fx_abs((ol + orr) / 2 - cx) + fx_abs((ot + ob) / 2 - cy);
        if (d < best_d) { best_d = d; best = o; }
    }
    return best;
}

void interact_use(Actor *o, Actor *user) {
    int k = actor_logic_index(o, BHV_INTERACT);
    if (k < 0) return;
    const Params_interact *p = o->logic[k].params;
    if (p->face_player && o->body) {
        s32 dx = fx_to_int(user->x - o->x), dy = fx_to_int(user->y - o->y);
        if (dx) o->facing_left = dx < 0;
        if (fx_abs(dy) > fx_abs(dx)) o->facing = dy < 0 ? DIR_UP : DIR_DOWN;
        else o->facing = dx < 0 ? DIR_LEFT : DIR_RIGHT;
        actor_play_slot(o, ANIM_IDLE);
    }
    actor_sound(o, SND_TALK);
    scripts_start(p->on_press, o);
}
