// ui.h - everything on BG0: text, the HUD, menus and dialog boxes.
// While a menu or dialog is open the world is paused.
#ifndef ENGINE_UI_H
#define ENGINE_UI_H

#include "data.h"

// Clear BG0 and close everything (scene change).
void ui_reset(void);
// Show a HUD (replacing the one on screen); it redraws itself whenever a game variable changes.
void ui_show_hud(const HudData *h);
// Cover the screen behind the UI with a drifting pattern (an 8x8 or 16x16 icon) for `ticks`
// (a sandstorm). The HUD stays readable on top. NULL or 0 ticks removes the cover.
void ui_cover(const IconData *icon, int ticks);
// Open a menu (closes any open dialog).
void ui_open_menu(const MenuData *m);
// Close the open menu.
void ui_close_menu(void);
// Open a dialog box (closes any open menu).
void ui_show_dialog(const DialogData *d);
// True while a menu or dialog is open.
int ui_blocking(void);
// Read buttons for the open menu/dialog, type text, refresh the HUD. Once per frame.
void ui_update(void);

#endif
