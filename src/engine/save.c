// save.c - SRAM save/load of the game variables. SRAM is 8 bits wide: byte access only.
// Layout: "GSV2", the variable count (2 bytes), then each variable's save key (2 bytes, made
// from its name) and value (4 bytes). Loading matches variables by key, so a save keeps working
// after a game update adds, removes or reorders variables (new ones keep their starting value).
#include "save.h"
#include "vars.h"
#include "data.h"

#define SRAM ((volatile u8 *)0x0E000000)
#define SAVE_DATA 8                 // first entry
#define SAVE_ENTRY 6                // key + value

#if GAME_SAVE
// Emulators look for this text in the ROM to know the cartridge has SRAM.
static const char s_save_tag[] __attribute__((aligned(4))) = "SRAM_V113";

static u32 sram_u32(int at, int bytes) {
    u32 x = 0;
    for (int b = 0; b < bytes; b++) x |= (u32)SRAM[at + b] << (8 * b);
    return x;
}

static void sram_put(int at, u32 x, int bytes) {
    for (int b = 0; b < bytes; b++) SRAM[at + b] = (u8)(x >> (8 * b));
}

static int sram_tag(const char *tag) {
    for (int i = 0; i < 4; i++)
        if (SRAM[i] != (u8)tag[i]) return 0;
    return 1;
}
#endif

void save_init(void) {
#if GAME_SAVE
    volatile const char *t = s_save_tag;
    (void)t[0];
#endif
}

void save_write(void) {
#if GAME_SAVE
    const s32 *v = vars_raw();
    SRAM[0] = 'G'; SRAM[1] = 'S'; SRAM[2] = 'V'; SRAM[3] = '2';
    sram_put(4, VAR_COUNT, 2);
    for (int i = 0; i < VAR_COUNT; i++) {
        sram_put(SAVE_DATA + i * SAVE_ENTRY, g_var_key[i], 2);
        sram_put(SAVE_DATA + i * SAVE_ENTRY + 2, (u32)v[i], 4);
    }
#endif
}

int save_read(void) {
#if GAME_SAVE
    s32 *v = vars_raw();
    if (sram_tag("GSAV") && SRAM[4] == VAR_COUNT) {         // older saves: values in variable order
        for (int i = 0; i < VAR_COUNT; i++) v[i] = (s32)sram_u32(8 + i * 4, 4);
        return 1;
    }
    if (!sram_tag("GSV2")) return 0;
    int count = (int)sram_u32(4, 2);
    if (count > 1024) return 0;
    for (int e = 0; e < count; e++) {
        u32 key = sram_u32(SAVE_DATA + e * SAVE_ENTRY, 2);
        for (int i = 0; i < VAR_COUNT; i++)
            if (g_var_key[i] == key) {
                v[i] = (s32)sram_u32(SAVE_DATA + e * SAVE_ENTRY + 2, 4);
                break;
            }
    }
    return 1;
#else
    return 0;
#endif
}
