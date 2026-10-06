// scene.c - load / unload scenes, spawn their instances, screen fades.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include "scene.h"
#include "core.h"
#include "actor.h"
#include "actions.h"
#include "anim.h"
#include "particles.h"
#include "camera.h"
#include "bg.h"
#include "sprites.h"
#include "audio.h"
#include "ui.h"

static const SceneData *s_scene;
static s16 s_scene_node = -1;
static s16 s_pending = -1;

static fixed s_fade;        // 0 = clear, 16 << 8 = black
static fixed s_fade_step;
static fixed s_fade_target;
static u8 s_dark;           // world darkness (night), 0..16; the UI layer is never darkened

const SceneData *scene_current(void) { return s_scene; }
s16 scene_current_node(void) { return s_scene_node; }

void scene_goto(s16 node) {
    if (node >= 0 && node < NODE_COUNT && g_nodes[node].type == NT_SCENE) s_pending = node;
}

void scene_fade(int to_black, int ticks) {
    s_fade_target = to_black ? fx_from_int(16) : 0;
    if (ticks <= 0) {
        s_fade = s_fade_target;
        s_fade_step = 0;
        return;
    }
    if (!to_black && s_fade == 0) s_fade = fx_from_int(16);   // fade_in always starts from black
    s_fade_step = fx_from_int(16) / ticks;
    if (s_fade_step == 0) s_fade_step = 1;
}

void scene_set_darkness(int level) {
    s_dark = (u8)(level < 0 ? 0 : level > 16 ? 16 : level);
}

void scene_vblank(void) {
    int fade = fx_to_int(s_fade);
    if (s_dark > fade) {                                        // night: darken all but BG0 (UI)
        REG_BLDCNT = BLD_BUILD(BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP, 0, 3);
        REG_BLDY = s_dark;
    } else {
        REG_BLDCNT = BLD_BUILD(BLD_ALL | BLD_BACKDROP, 0, 3);  // mode 3 = fade to black
        REG_BLDY = (u16)fade;
    }
}

Actor *scene_spawn(s16 node, s32 x, s32 y) {
    if (node < 0 || node >= NODE_COUNT) return NULL;
    const NodeEntry *e = &g_nodes[node];
    if (e->type == NT_PARTICLE) {
        particles_burst((const ParticleData *)e->data, x, y);
        return NULL;
    }
    if (e->type < NT_PLAYER || e->type > NT_TRIGGER) return NULL;
    const ActorData *ad = (const ActorData *)e->data;
    return actor_spawn(node, x, y, ad->logic, ad->logic_count, -1);
}

static void load_scene(s16 node) {
    const SceneData *sc = (const SceneData *)g_nodes[node].data;
    // Unload everything from the previous scene.
    scripts_clear();
    actor_clear_all();
    particles_clear();
    ui_reset();
    sprites_reset();
    bg_reset(sc->backdrop);
    s_scene = sc;
    s_scene_node = node;
    s_fade = 0;
    s_fade_step = 0;
    s_fade_target = 0;
    s_dark = 0;

    camera_reset(sc->cam_x, sc->cam_y, sc->camera_bounds);

    // Place every instance, in order.
    Actor *follow = NULL;
    for (int i = 0; i < sc->instance_count; i++) {
        const InstanceData *in = &sc->instances[i];
        switch (in->kind) {
        case INST_TILEMAP:  bg_set_layer((const TilemapData *)in->data); break;
        case INST_HUD:      ui_show_hud((const HudData *)in->data); break;
        case INST_MENU:     ui_open_menu((const MenuData *)in->data); break;
        case INST_DIALOG:   ui_show_dialog((const DialogData *)in->data); break;
        case INST_PARTICLE: particles_place((const ParticleData *)in->data, in->x, in->y); break;
        case INST_ACTOR: {
            Actor *a = actor_spawn(in->node, in->x, in->y, in->logic, in->logic_count, (s16)i);
            if (i == sc->camera_follow) follow = a;
            break;
        }
        }
    }

    if (follow) camera_set_target(follow);

    if (sc->music >= 0) audio_play_music((const MusicData *)g_nodes[sc->music].data);
    else audio_stop_music();

    scripts_start(sc->on_start, NULL);
    if (sc->on_start_fn) sc->on_start_fn();

    camera_update();
    bg_refresh_all();
}

void scene_update(void) {
    if (s_pending >= 0) {
        s16 n = s_pending;
        s_pending = -1;
        load_scene(n);
    }
    if (s_fade != s_fade_target) {
        if (s_fade < s_fade_target) {
            s_fade += s_fade_step;
            if (s_fade > s_fade_target) s_fade = s_fade_target;
        } else {
            s_fade -= s_fade_step;
            if (s_fade < s_fade_target) s_fade = s_fade_target;
        }
    }
    scripts_update();
    if (!core_paused()) {
        actor_update_all();
        if (s_scene && s_scene->on_update_fn) s_scene->on_update_fn();
    }
}
