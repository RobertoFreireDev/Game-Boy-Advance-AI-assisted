// ui.c - text, HUD, menus and dialogs on BG0 (charblock 0, screenblock 31).
//
// Charblock 0 tile plan:
//   0          blank
//   1..127     font glyphs, transparent background (HUD, menus without a box)
//   128..255   font glyphs on the box "paper" color (menus with a box, dialogs)
//   256..264   box frame (3x3: corners, edges, fill)
//   265..273   bar fill levels 0/8 .. 8/8
//   288..511   icons
#include <string.h>
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include <tonc_core.h>
#include "ui.h"
#include "config.h"
#include "core.h"
#include "input.h"
#include "vars.h"
#include "bg.h"
#include "audio.h"
#include "actions.h"

#define SBB 31
#define T_FONT   1
#define T_PAPER  128
#define T_FRAME  256
#define T_BAR    265
#define T_ICONS  288
#define T_END    512

typedef struct { const IconData *icon; u16 base; u8 bank, paper; } IconSlot;   // paper 0xFF = transparent

static const FontData *s_font, *s_paper_font;
static u8 s_font_bank, s_paper_bank, s_paper, s_border;
static IconSlot s_icons[MAX_ICONS];
static int s_icon_count, s_icon_next;
static u8 s_bar_color = 0xFF, s_bar_back;

static const HudData *s_hud;
static u32 s_hud_version;

static const MenuData *s_menu;
static u8 s_cursor, s_menu_locked;

static const DialogData *s_dlg;
static u8 s_line, s_typing, s_choosing, s_choice;
static const char *s_text_pos;
static u8 s_cx, s_cy;
static u16 s_tick;

static u32 s_opened;        // frame a menu/dialog opened (ignore buttons on that frame)

// ---- low level -----------------------------------------------------------------------
static void put(int x, int y, u16 e) {
    if (x >= 0 && x < 30 && y >= 0 && y < 20) se_mem[SBB][y * 32 + x] = e;
}

static void clear_rect(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, 0);
}

static void load_font(const FontData *f) {
    if (s_font == f) return;
    s_font = f;
    s_font_bank = (u8)bg_pal_bank(f->palette);
    int n = f->glyph_count > 127 ? 127 : f->glyph_count;
    memcpy32(&tile_mem[0][T_FONT], f->tiles, n * 8);
}

// One row of 4bpp pixels with the transparent ones painted in `paper`.
static u32 on_paper(u32 w, u8 paper) {
    for (int k = 0; k < 8; k++)
        if (((w >> (4 * k)) & 15) == 0) w |= (u32)paper << (4 * k);
    return w;
}

// Glyphs with transparent pixels painted in the paper color, so text sits on the box.
static void load_paper_font(const FontData *f, u8 paper, u8 border) {
    if (s_paper_font == f && s_paper == paper && s_border == border) return;    // already in VRAM
    s_paper = paper;
    s_border = border;
    s_paper_bank = (u8)bg_pal_bank(f->palette);
    s_paper_font = f;
    int n = f->glyph_count > 127 ? 127 : f->glyph_count;
    u32 *dst = (u32 *)&tile_mem[0][T_PAPER];
    for (int i = 0; i < n * 8; i++) dst[i] = on_paper(f->tiles[i], paper);
    // Box frame: 2-pixel border with cut corners, paper inside.
    for (int t = 0; t < 9; t++) {
        int left = t % 3 == 0, right = t % 3 == 2, top = t / 3 == 0, bottom = t / 3 == 2;
        u32 *d = (u32 *)&tile_mem[0][T_FRAME + t];
        for (int y = 0; y < 8; y++) {
            u32 w = 0;
            for (int x = 0; x < 8; x++) {
                int edge = (left && x < 2) || (right && x > 5) || (top && y < 2) || (bottom && y > 5);
                int cut = (left || right) && (top || bottom) &&
                          (x == (left ? 0 : 7)) && (y == (top ? 0 : 7));
                u32 c = cut ? 0 : edge ? border : paper;
                w |= c << (4 * x);
            }
            d[y] = w;
        }
    }
}

static void load_bar_tiles(u8 color, u8 back) {
    if (s_bar_color == color && s_bar_back == back) return;
    s_bar_color = color;
    s_bar_back = back;
    for (int level = 0; level <= 8; level++) {
        u32 *d = (u32 *)&tile_mem[0][T_BAR + level];
        for (int y = 0; y < 8; y++) {
            u32 w = 0;
            if (y >= 1 && y <= 6)
                for (int x = 0; x < 8; x++) w |= (u32)(x < level ? color : back) << (4 * x);
            d[y] = w;
        }
    }
}

// Icon tiles in VRAM. paper != 0xFF paints the transparent pixels (when the icon shares the
// box font's palette), so icons drawn on a box have no holes.
static const IconSlot *icon_slot(const IconData *ic, u8 paper) {
    if (paper != 0xFF && (!s_paper_font || ic->palette != s_paper_font->palette)) paper = 0xFF;
    for (int i = 0; i < s_icon_count; i++)
        if (s_icons[i].icon == ic && s_icons[i].paper == paper) return &s_icons[i];
    int tiles = (ic->width / 8) * (ic->height / 8);
    if (s_icon_count >= MAX_ICONS || s_icon_next + tiles > T_END) return NULL;
    IconSlot *s = &s_icons[s_icon_count++];
    s->icon = ic;
    s->base = (u16)s_icon_next;
    s->bank = (u8)bg_pal_bank(ic->palette);
    s->paper = paper;
    u32 *dst = (u32 *)&tile_mem[0][s_icon_next];
    for (int i = 0; i < tiles * 8; i++) dst[i] = paper == 0xFF ? ic->tiles[i] : on_paper(ic->tiles[i], paper);
    s_icon_next += tiles;
    return s;
}

static void draw_icon(int x, int y, const IconData *ic, u8 paper) {
    const IconSlot *s = icon_slot(ic, paper);
    if (!s) return;
    int w = ic->width / 8, h = ic->height / 8;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put(x + i, y + j, (u16)((s->base + j * w + i) | (s->bank << 12)));
}

static u16 glyph(char c, int paper) {
    const FontData *f = paper ? s_paper_font : s_font;
    u8 g = f ? f->map[(u8)c & 127] : 0;
    if (!g) return paper ? (u16)((T_FRAME + 4) | (s_paper_bank << 12)) : 0;
    return paper ? (u16)((T_PAPER + g - 1) | (s_paper_bank << 12)) : (u16)((T_FONT + g - 1) | (s_font_bank << 12));
}

// Draw text; \n starts a new line, \001 + (var+1) prints a variable, \002 + (var+1) prints it
// with at least 2 digits (05). Returns columns used.
static int draw_text(int x, int y, const char *s, int paper) {
    int cx = x;
    for (; *s; s++) {
        if (*s == '\n') { y++; cx = x; continue; }
        if ((*s == '\001' || *s == '\002') && s[1]) {
            char buf[12];
            int digits = *s == '\002' ? 2 : 1;
            s32 v = vars_get((s16)((u8)s[1] - 1));
            s++;
            int neg = v < 0, n = 0;
            u32 u = neg ? (u32)-v : (u32)v;
            do { buf[n++] = (char)('0' + u % 10); u /= 10; } while ((u || n < digits) && n < 10);
            if (neg) buf[n++] = '-';
            while (n) put(cx++, y, glyph(buf[--n], paper));
            continue;
        }
        put(cx++, y, glyph(*s, paper));
    }
    return cx - x;
}

static void draw_box(const UiBox *b) {
    for (int j = 0; j < b->h; j++) {
        int row = j == 0 ? 0 : j == b->h - 1 ? 2 : 1;
        for (int i = 0; i < b->w; i++) {
            int col = i == 0 ? 0 : i == b->w - 1 ? 2 : 1;
            put(b->x + i, b->y + j, (u16)((T_FRAME + row * 3 + col) | (s_paper_bank << 12)));
        }
    }
}

static void fill_paper(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, (u16)((T_FRAME + 4) | (s_paper_bank << 12)));
}

// ---- HUD -----------------------------------------------------------------------------
static int text_span(const char *s) {
    int n = 0;
    for (; *s; s++) {
        if ((*s == '\001' || *s == '\002') && s[1]) { s++; n += 3; }
        else n++;
    }
    return n;
}

static void draw_hud(void) {
    const HudData *h = s_hud;
    if (!h) return;
    load_font(h->font);
    for (int i = 0; i < h->count; i++) {
        const HudElement *e = &h->elements[i];
        switch (e->kind) {
        case HUD_TEXT:
            clear_rect(e->x, e->y, text_span(e->text) + 3, 1);
            draw_text(e->x, e->y, e->text, 0);
            break;
        case HUD_ICON:
            draw_icon(e->x, e->y, e->icon, 0xFF);
            break;
        case HUD_ICON_REPEAT: {
            s32 v = vars_get(e->var);
            int size = e->icon->width / 8;
            for (int k = 0; k < e->max; k++) {
                int x = e->x + k * e->spacing;
                if (k < v) draw_icon(x, e->y, e->icon, 0xFF);
                else if (e->empty_icon) draw_icon(x, e->y, e->empty_icon, 0xFF);
                else clear_rect(x, e->y, size, size);
            }
            break;
        }
        case HUD_BAR: {
            load_bar_tiles(e->color, e->back);
            s32 v = vars_get(e->var);
            if (v < 0) v = 0;
            if (v > e->max) v = e->max;
            s32 px = v * e->length * 8 / e->max;
            u8 bank = (u8)bg_pal_bank(h->font->palette);
            for (int k = 0; k < e->length; k++) {
                s32 lvl = px - k * 8;
                lvl = lvl < 0 ? 0 : lvl > 8 ? 8 : lvl;
                put(e->x + k, e->y, (u16)((T_BAR + lvl) | (bank << 12)));
            }
            break;
        }
        }
    }
    s_hud_version = vars_version();
}

void ui_show_hud(const HudData *h) {
    s_hud = h;
    draw_hud();
}

// ---- upgrade menus: pick up to N random upgrades the player can take -----------------
#define UPG_ICON_DX 3       // the card icon sits 3 columns left of the text (2 wide + 1 gap)
#define UPG_TAG_DX  15      // "NEW!" / "LV2" / "EVOLVE!" column, right of the title

static s16 s_up[MAX_UPGRADE_CHOICES];
static u8 s_up_count;
static s16 s_upg_nodes[NODE_COUNT];     // every upgrade node, found once
static s16 s_upg_total = -1;

static void upg_find_all(void) {
    if (s_upg_total >= 0) return;
    s_upg_total = 0;
    for (int n = 0; n < NODE_COUNT; n++)
        if (g_nodes[n].type == NT_UPGRADE) s_upg_nodes[s_upg_total++] = (s16)n;
}

static const UpgradeData *upg(s16 node) { return (const UpgradeData *)g_nodes[node].data; }
static s32 upg_level(const UpgradeData *u) { return u->var >= 0 ? vars_get(u->var) : 0; }

// Can this weapon / item / evolution be offered now?
static int upg_available(s16 node) {
    const UpgradeData *u = upg(node);
    if (upg_level(u) >= u->max_level) return 0;
    for (int i = 0; i < u->req_count; i++)
        if (upg_level(upg(u->reqs[i].upgrade)) < u->reqs[i].level) return 0;
    for (int k = 0; k < s_upg_total; k++) {     // replaced by an evolution the player owns
        const UpgradeData *e = upg(s_upg_nodes[k]);
        if (e->replaces == node && upg_level(e) > 0) return 0;
    }
    return 1;
}

// Class 0 = evolutions, 1 = weapons and items, 2 = bonus fillers.
static int upg_in_class(s16 node, int cls) {
    u8 c = upg(node)->category;
    if (cls == 2) return c == UPG_BONUS;
    if (c == UPG_BONUS || (c == UPG_EVOLUTION) != (cls == 0)) return 0;
    return upg_available(node);
}

static int upg_picked(s16 node) {
    for (int i = 0; i < s_up_count; i++)
        if (s_up[i] == node) return 1;
    return 0;
}

static int upg_candidate(s16 n, int cls) {
    return !upg_picked(n) && upg_in_class(n, cls);
}

// Add random upgrades of one class until there are `want` choices (or none are left).
static void upg_pick_class(int cls, int want) {
    while (s_up_count < want) {
        int k = 0;
        for (int i = 0; i < s_upg_total; i++)
            if (upg_candidate(s_upg_nodes[i], cls)) k++;
        if (!k) return;
        int r = rng_range(0, k - 1);
        for (int i = 0; i < s_upg_total; i++)
            if (upg_candidate(s_upg_nodes[i], cls) && r-- == 0) {
                s_up[s_up_count++] = s_upg_nodes[i];
                break;
            }
    }
}

// Evolutions first (they are rare and earned), then weapons/items, then bonus fillers.
static void upg_pick(int want) {
    if (want > MAX_UPGRADE_CHOICES) want = MAX_UPGRADE_CHOICES;
    upg_find_all();
    s_up_count = 0;
    for (int cls = 0; cls < 3; cls++) upg_pick_class(cls, want);
}

static void draw_upgrade_card(int i) {
    const MenuOption *slot = &s_menu->options[i];
    const UpgradeData *u = upg(s_up[i]);
    s32 lv = upg_level(u);
    char tag[8] = "";
    if (u->category == UPG_EVOLUTION) memcpy(tag, "EVOLVE!", 8);
    else if (u->category != UPG_BONUS && lv == 0) memcpy(tag, "NEW!", 5);
    else if (u->category != UPG_BONUS) { tag[0] = 'L'; tag[1] = 'V'; tag[2] = (char)('1' + (lv > 8 ? 8 : lv)); }
    int d = lv < u->desc_count ? lv : u->desc_count - 1;
    if (u->icon) draw_icon(slot->x - UPG_ICON_DX, slot->y, u->icon, s_menu->box.paper);
    draw_text(slot->x, slot->y, u->title, 1);
    draw_text(slot->x + UPG_TAG_DX, slot->y, tag, 1);
    if (d >= 0) draw_text(slot->x, slot->y + 1, u->descriptions[d], 1);
}

// Level up the picked upgrade (an evolution also removes the weapon it replaces).
static void apply_upgrade(s16 node) {
    const UpgradeData *u = upg(node);
    if (u->var >= 0) vars_add(u->var, 1);
    if (u->replaces >= 0 && upg(u->replaces)->var >= 0) vars_set(upg(u->replaces)->var, 0);
    ui_close_menu();
    scripts_start(u->on_pick, NULL);
}

// ---- menus ---------------------------------------------------------------------------
static int cursor_w(void) { return s_menu->cursor ? s_menu->cursor->width / 8 : 1; }
static int menu_count(void) { return s_menu->upgrade_count ? s_up_count : s_menu->option_count; }

static void draw_menu_cursor(void) {
    const MenuData *m = s_menu;
    int cw = cursor_w();
    for (int i = 0; i < menu_count(); i++) {
        const MenuOption *o = &m->options[i];
        int x = o->x - cw - (m->upgrade_count ? UPG_ICON_DX : 0);
        if (i == s_cursor && m->cursor) {
            draw_icon(x, o->y, m->cursor, m->has_box ? m->box.paper : 0xFF);
        } else if (m->has_box) {
            fill_paper(x, o->y, cw, m->cursor ? m->cursor->height / 8 : 1);
        } else {
            clear_rect(x, o->y, cw, m->cursor ? m->cursor->height / 8 : 1);
        }
    }
}

static void draw_menu(void) {
    const MenuData *m = s_menu;
    int paper = m->has_box;
    if (paper) {
        load_paper_font(m->font, m->box.paper, m->box.border);
        draw_box(&m->box);
    } else {
        load_font(m->font);
    }
    if (m->title) draw_text(m->title_x, m->title_y, m->title, paper);
    for (int i = 0; i < menu_count(); i++) {
        if (m->upgrade_count) draw_upgrade_card(i);
        else draw_text(m->options[i].x, m->options[i].y, m->options[i].label, paper);
    }
    draw_menu_cursor();
}

static void close_box(void) {
    memset32(&se_mem[SBB][0], 0, 512);
    draw_hud();
}

void ui_open_menu(const MenuData *m) {
    if (m->upgrade_count) {
        upg_pick(m->upgrade_count);
        if (!s_up_count) return;                // nothing left to offer: skip the menu
    }
    if (s_dlg) { s_dlg = NULL; close_box(); }
    if (s_menu) close_box();
    s_menu = m;
    s_cursor = 0;
    s_menu_locked = 0;
    s_opened = core_frame();
    draw_menu();
}

void ui_close_menu(void) {
    if (!s_menu) return;
    s_menu = NULL;
    close_box();
}

static void menu_update(void) {
    const MenuData *m = s_menu;
    if (s_menu_locked || s_opened == core_frame()) return;
    int dir = input_pressed(KEY_DOWN) - input_pressed(KEY_UP);
    if (dir) {
        s_cursor = (u8)((s_cursor + dir + menu_count()) % menu_count());
        if (m->sfx_move >= 0) audio_play_sfx((const SfxData *)g_nodes[m->sfx_move].data);
        draw_menu_cursor();
    } else if (input_pressed(KEY_A)) {
        if (m->sfx_select >= 0) audio_play_sfx((const SfxData *)g_nodes[m->sfx_select].data);
        if (m->upgrade_count) {                 // upgrade cards apply themselves and close
            apply_upgrade(s_up[s_cursor]);
            return;
        }
        // The menu stops taking input; its actions decide what happens (close_menu, goto_scene...).
        s_menu_locked = 1;
        scripts_start(m->options[s_cursor].actions, NULL);
    } else if (input_pressed(KEY_B) && m->on_cancel.count) {
        s_menu_locked = 1;
        scripts_start(m->on_cancel, NULL);
    }
}

// ---- dialogs -------------------------------------------------------------------------
static void start_line(void) {
    const DialogData *d = s_dlg;
    const DialogLine *ln = &d->lines[s_line];
    draw_box(&d->box);
    if (ln->speaker) draw_text(ln->speaker_x, ln->speaker_y, ln->speaker, 1);
    if (ln->portrait) draw_icon(ln->portrait_x, ln->portrait_y, ln->portrait, d->box.paper);
    s_text_pos = ln->text;
    s_cx = ln->text_x;
    s_cy = ln->text_y;
    s_tick = 0;
    s_typing = 1;
    if (d->ticks_per_char == 0) {
        draw_text(s_cx, s_cy, s_text_pos, 1);
        s_typing = 0;
    }
}

static void type_char(void) {
    const DialogLine *ln = &s_dlg->lines[s_line];
    char c = *s_text_pos++;
    if (c == '\n') { s_cy++; s_cx = ln->text_x; }
    else put(s_cx++, s_cy, glyph(c, 1));
    if (!*s_text_pos) s_typing = 0;
}

static void draw_choices(void) {
    const DialogData *d = s_dlg;
    for (int i = 0; i < d->choice_count; i++) {
        const MenuOption *o = &d->choices[i];
        put(o->x - 1, o->y, glyph(i == s_choice ? '>' : ' ', 1));
        draw_text(o->x, o->y, o->label, 1);
    }
}

void ui_show_dialog(const DialogData *d) {
    if (s_menu) { s_menu = NULL; close_box(); }
    s_dlg = d;
    s_line = 0;
    s_choosing = 0;
    s_choice = 0;
    s_opened = core_frame();
    load_paper_font(d->font, d->box.paper, d->box.border);
    start_line();
}

static void end_dialog(ActionList choice_actions) {
    const DialogData *d = s_dlg;
    s_dlg = NULL;
    close_box();
    scripts_start(choice_actions, NULL);
    scripts_start(d->on_end, NULL);
}

static void dialog_update(void) {
    const DialogData *d = s_dlg;
    if (s_opened == core_frame()) return;
    if (s_choosing) {
        int dir = input_pressed(KEY_DOWN) - input_pressed(KEY_UP);
        if (dir) {
            s_choice = (u8)((s_choice + dir + d->choice_count) % d->choice_count);
            draw_choices();
        } else if (input_pressed(KEY_A)) {
            end_dialog(d->choices[s_choice].actions);
        }
        return;
    }
    if (s_typing) {
        if (input_pressed(KEY_A | KEY_B)) {
            while (s_typing) type_char();
        } else if (++s_tick >= d->ticks_per_char) {
            s_tick = 0;
            type_char();
        }
        return;
    }
    if (!input_pressed(KEY_A | KEY_B)) return;
    if (s_line + 1 < d->line_count) {
        s_line++;
        start_line();
    } else if (d->choice_count) {
        s_choosing = 1;
        draw_box(&d->box);
        draw_choices();
    } else {
        ActionList none = { NULL, 0 };
        end_dialog(none);
    }
}

// ---- frame ---------------------------------------------------------------------------
void ui_reset(void) {
    memset32(&se_mem[SBB][0], 0, 512);
    memset32(&tile_mem[0][0], 0, 8);
    REG_BG0CNT = BG_CBB(0) | BG_SBB(SBB) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    s_font = s_paper_font = NULL;
    s_icon_count = 0;
    s_icon_next = T_ICONS;
    s_bar_color = 0xFF;
    s_hud = NULL;
    s_menu = NULL;
    s_dlg = NULL;
}

int ui_blocking(void) { return s_menu != NULL || s_dlg != NULL; }

void ui_update(void) {
    if (s_dlg) dialog_update();
    else if (s_menu) menu_update();
    if (s_hud && s_hud_version != vars_version() && !s_dlg && !s_menu) draw_hud();
}
