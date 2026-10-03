// core.c - boot and the main loop.
#include <tonc_irq.h>
#include <tonc_bios.h>
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include "core.h"
#include "fixed.h"
#include "data.h"
#include "input.h"
#include "vars.h"
#include "scene.h"
#include "actor.h"
#include "physics.h"
#include "anim.h"
#include "particles.h"
#include "camera.h"
#include "bg.h"
#include "sprites.h"
#include "audio.h"
#include "ui.h"
#include "save.h"

static u32 s_frame;
static u32 s_rng = 0x2545F491;
static int s_paused;

u32 rng_next(void) {
    u32 x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

s32 rng_range(s32 lo, s32 hi) {
    if (hi <= lo) return lo;
    return lo + (s32)(rng_next() % (u32)(hi - lo + 1));
}

u32 core_frame(void) { return s_frame; }
int core_paused(void) { return s_paused; }

void core_init(void) {
    REG_WAITCNT = WS_STANDARD;      // faster cartridge reads (ROM 3/1 wait states + prefetch)
    irq_init(NULL);
    irq_add(II_VBLANK, NULL);
    vars_init();
    audio_init();
    save_init();
    scene_goto(g_game.start_scene);
}

void core_run(void) {
    for (;;) {
        // 1. VBlank: show what the previous frame prepared.
        VBlankIntrWait();
        sprites_vblank();
        bg_vblank();
        scene_vblank();
        s_frame++;
        s_rng ^= s_frame;
        // 2. Buttons.
        input_update();
        s_paused = ui_blocking();
        // 3. Scene (pending scene switch, action lists) and actor logic.
        scene_update();
        s_paused = ui_blocking();
        if (!s_paused) {
            // 4. Movement and collisions.
            physics_update();
            // 5. Animations and particles.
            anim_update();
            particles_update();
        }
        // 6. Camera.
        camera_update();
        // 7. HUD, menus, dialogs.
        ui_update();
        // 8. Music and sound effects.
        audio_update();
        // 9. Sprite list for the next VBlank.
        sprites_build();
    }
}
