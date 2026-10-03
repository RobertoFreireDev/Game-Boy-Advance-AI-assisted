// shooter - every few ticks, while the player is in range, fires one or more objects at the
// player (fanned out by 'spread' degrees). A shot with the projectile behavior flies at its own
// speed; any other object is pushed at 'speed'. Give enemy shots damage_on_touch (vanish).
#include "shooter.h"
#include "behavior_params.h"
#include "projectile.h"
#include "engine/scene.h"
#include "engine/audio.h"

enum { ST_TIMER };

void bhv_shooter_init(Actor *a, const void *params) {
    const Params_shooter *p = params;
    bhv_state(a)[ST_TIMER] = (u32)p->every;
}

void bhv_shooter_update(Actor *a, const void *params) {
    const Params_shooter *p = params;
    u32 *st = bhv_state(a);
    if (a->anim_lock || a->stun) return;
    if (st[ST_TIMER] && --st[ST_TIMER]) return;
    st[ST_TIMER] = (u32)p->every;
    Actor *pl = actor_player();
    if (!pl) return;
    s32 x = fx_to_int(a->x), y = fx_to_int(a->y) + p->offset_y;
    s32 dx = fx_to_int(pl->x) - x, dy = fx_to_int(pl->y) + pl->hb_y + pl->hb_h / 2 - y;
    if (fx_abs(dx) > p->range || fx_abs(dy) > p->range) return;
    s32 len = (s32)fx_isqrt((u32)(dx * dx + dy * dy));
    fixed ux = len ? dx * FX_ONE / len : FX_ONE, uy = len ? dy * FX_ONE / len : 0;
    a->facing_left = dx < 0;
    for (int k = 0; k < p->count; k++) {
        // Fan the volley around the aim: rotate (ux, uy) by (k - (count-1)/2) * spread degrees.
        s32 ang = (2 * k - (p->count - 1)) * p->spread / 2;
        fixed c = fx_cos_deg(ang), s = fx_sin_deg(ang);
        fixed rx = fx_mul(ux, c) - fx_mul(uy, s), ry = fx_mul(ux, s) + fx_mul(uy, c);
        Actor *shot = scene_spawn(p->object, x, y);
        if (!shot) break;
        if (actor_logic_index(shot, BHV_PROJECTILE) >= 0) {
            projectile_launch(shot, a, rx, ry, 0, 0);
        } else {
            shot->vx = fx_mul(rx, p->speed);
            shot->vy = fx_mul(ry, p->speed);
        }
        shot->facing_left = rx < 0;
    }
    if (p->sound) audio_play_sfx(p->sound);
    if (a->body && a->body->anims[ANIM_ATTACK]) {
        a->anim_slot = ANIM_ATTACK;
        anim_start(&a->anim, a->body->anims[ANIM_ATTACK], a);
        a->anim_lock = (u16)anim_length(a->body->anims[ANIM_ATTACK]);
    }
}
