// actions.c - action list interpreter (see actions.h).
#include "actions.h"
#include "config.h"
#include "actor.h"
#include "vars.h"
#include "scene.h"
#include "audio.h"
#include "camera.h"
#include "ui.h"
#include "save.h"

typedef struct {
    const Action *items;
    u16 count, pc;
} Frame;

typedef struct {
    u8 active;
    s8 depth;
    u16 wait;
    struct Actor *self;
    Frame stack[SCRIPT_DEPTH];
} Script;

static Script s_scripts[MAX_SCRIPTS];

void scripts_clear(void) {
    for (int i = 0; i < MAX_SCRIPTS; i++) s_scripts[i].active = 0;
}

void scripts_forget_actor(struct Actor *a) {
    for (int i = 0; i < MAX_SCRIPTS; i++)
        if (s_scripts[i].active && s_scripts[i].self == a) s_scripts[i].self = NULL;
}

static int compare(s32 v, u8 cmp, s32 w) {
    switch (cmp) {
    case CMP_EQ: return v == w;
    case CMP_NE: return v != w;
    case CMP_LT: return v < w;
    case CMP_LE: return v <= w;
    case CMP_GT: return v > w;
    case CMP_GE: return v >= w;
    }
    return 0;
}

static void push(Script *s, ActionList list) {
    if (list.count == 0 || s->depth + 1 >= SCRIPT_DEPTH) return;
    s->depth++;
    s->stack[s->depth].items = list.items;
    s->stack[s->depth].count = list.count;
    s->stack[s->depth].pc = 0;
}

// Run actions until the script waits or ends. Returns 0 when the script is finished.
static int run(Script *s) {
    while (s->depth >= 0) {
        Frame *f = &s->stack[s->depth];
        if (f->pc >= f->count) {
            s->depth--;
            continue;
        }
        const Action *a = &f->items[f->pc++];
        struct Actor *self = s->self;
        switch (a->op) {
        case ACT_GOTO_SCENE:
            scene_goto(a->node);
            return 0;
        case ACT_FADE_IN:
            scene_fade(0, a->a);
            if (a->a > 0) { s->wait = (u16)a->a; return 1; }
            break;
        case ACT_FADE_OUT:
            scene_fade(1, a->a);
            if (a->a > 0) { s->wait = (u16)a->a; return 1; }
            break;
        case ACT_WAIT:
            if (a->a > 0) { s->wait = (u16)a->a; return 1; }
            break;
        case ACT_PLAY_SFX:
            if (a->node >= 0) audio_play_sfx((const SfxData *)g_nodes[a->node].data);
            break;
        case ACT_PLAY_MUSIC:
            if (a->node >= 0) audio_play_music((const MusicData *)g_nodes[a->node].data);
            break;
        case ACT_STOP_MUSIC:
            audio_stop_music();
            break;
        case ACT_SET_VAR:
            vars_set(a->var, a->var_from >= 0 ? vars_get(a->var_from) : a->a);
            break;
        case ACT_ADD_VAR: {
            s32 v = vars_get(a->var) + (a->var_from >= 0 ? vars_get(a->var_from) : a->a) * a->b;   // b = times
            if (a->var_max >= 0 && v > vars_get(a->var_max)) v = vars_get(a->var_max);
            vars_set(a->var, v);
            break;
        }
        case ACT_RESET_VARS:
            vars_reset();
            break;
        case ACT_IF_VAR:
            push(s, compare(vars_get(a->var), a->sub, a->a) ? a->then_list : a->else_list);
            break;
        case ACT_IF_CHANCE:
            push(s, rng_range(1, 100) <= a->a ? a->then_list : a->else_list);
            break;
        case ACT_SHOW_DIALOG:
            if (a->node >= 0) ui_show_dialog((const DialogData *)g_nodes[a->node].data);
            break;
        case ACT_OPEN_MENU:
            if (a->node >= 0) ui_open_menu((const MenuData *)g_nodes[a->node].data);
            break;
        case ACT_CLOSE_MENU:
            ui_close_menu();
            break;
        case ACT_SPAWN: {
            s32 x = a->a, y = a->b;
            if (a->sub && self) {
                x += fx_to_int(self->x);
                y += fx_to_int(self->y);
            }
            scene_spawn(a->node, x, y);
            break;
        }
        case ACT_DESTROY_SELF:
            if (self) {
                s->self = NULL;
                actor_destroy(self);
            }
            break;
        case ACT_SHAKE_CAMERA:
            camera_shake(a->a, a->b);
            break;
        case ACT_SET_DARKNESS:
            scene_set_darkness(a->a);
            break;
        case ACT_SAVE_GAME:
            save_write();
            break;
        case ACT_LOAD_GAME:
            save_read();
            break;
        case ACT_CALL:
            if (a->fn) a->fn(self);
            break;
        }
    }
    return 0;
}

void scripts_start(ActionList list, struct Actor *self) {
    if (list.count == 0) return;
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        Script *s = &s_scripts[i];
        if (s->active) continue;
        s->active = 1;
        s->self = self;
        s->wait = 0;
        s->depth = 0;
        s->stack[0].items = list.items;
        s->stack[0].count = list.count;
        s->stack[0].pc = 0;
        s->active = (u8)run(s);
        return;
    }
}

void scripts_update(void) {
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        Script *s = &s_scripts[i];
        if (!s->active) continue;
        if (s->wait > 0 && --s->wait > 0) continue;
        s->active = (u8)run(s);
    }
}
