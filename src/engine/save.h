// save.h - save / load every game variable to cartridge SRAM (only when game.save is true).
#ifndef ENGINE_SAVE_H
#define ENGINE_SAVE_H

// Mark the ROM as using SRAM (so emulators create a save file).
void save_init(void);
// Write every variable to SRAM.
void save_write(void);
// Read the variables back. Returns 1 if a save existed.
int save_read(void);

#endif
