// save.c - SRAM save/load of the game variables. SRAM is 8 bits wide: byte access only.
#include "save.h"
#include "vars.h"
#include "data.h"

#define SRAM ((volatile u8 *)0x0E000000)

#if GAME_SAVE
// Emulators look for this text in the ROM to know the cartridge has SRAM.
static const char s_save_tag[] __attribute__((aligned(4))) = "SRAM_V113";
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
    SRAM[0] = 'G'; SRAM[1] = 'S'; SRAM[2] = 'A'; SRAM[3] = 'V';
    SRAM[4] = (u8)VAR_COUNT;
    for (int i = 0; i < VAR_COUNT; i++)
        for (int b = 0; b < 4; b++) SRAM[8 + i * 4 + b] = (u8)((u32)v[i] >> (8 * b));
#endif
}

int save_read(void) {
#if GAME_SAVE
    if (SRAM[0] != 'G' || SRAM[1] != 'S' || SRAM[2] != 'A' || SRAM[3] != 'V' || SRAM[4] != VAR_COUNT) return 0;
    s32 *v = vars_raw();
    for (int i = 0; i < VAR_COUNT; i++) {
        u32 x = 0;
        for (int b = 0; b < 4; b++) x |= (u32)SRAM[8 + i * 4 + b] << (8 * b);
        v[i] = (s32)x;
    }
    return 1;
#else
    return 0;
#endif
}
