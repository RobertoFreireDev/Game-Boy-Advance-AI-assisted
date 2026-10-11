// ui.c - text, HUD, menus and dialogs on BG0 (charblock 0, screenblock 31).
//
// Charblock 0 tile plan:
//   0          blank
//   1..127     font glyphs, transparent background (HUD, menus without a box)
//   128..255   font glyphs on the box "paper" color (menus with a box, dialogs)
//   256..264   box frame (3x3: corners, edges, fill)
//   265..300   bar fill levels 0/8 .. 8/8, up to 4 color sets of 9
//   304..511   icons
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
#define BAR_SETS 4
#define T_ICONS  304
#define T_END    512
#define TEXT_MAX 64         // characters of one UI text once its placeholders are filled in

typedef struct { const IconData *icon; u16 base; u8 bank, paper; } IconSlot;   // paper 0xFF = transparent

static const FontData *s_font, *s_paper_font;
static u8 s_font_bank, s_paper_bank, s_paper, s_border;
static IconSlot s_icons[MAX_ICONS];
static int s_icon_count, s_icon_next;
static u8 s_bar_sets[BAR_SETS][2];      // color, back of each bar tile set in VRAM
static int s_bar_set_count;

static const HudData *s_hud;
static u32 s_hud_version;
static u8 s_hud_blinks;                 // the HUD has blinking text: check it every frame

// Cover (a sandstorm): every empty UI cell shows a drifting pattern instead of the world.
static const IconSlot *s_cover;
static u16 s_cover_ticks;
EWRAM_BSS static u8 s_cover_px[16 * 16];    // the pattern's pixels (16x16, repeats), scrolled as it drifts

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

// One row of a box: `left`, then `mid` repeated, then `right` (w tiles), clipped to the screen.
// Rows are written straight into the map: a menu box is hundreds of tiles.
static void put_row(int x, int y, int w, u16 left, u16 mid, u16 right) {
    if (y < 0 || y >= 20 || w <= 0) return;
    u16 *row = &se_mem[SBB][y * 32];
    for (int i = x < 0 ? -x : 0; i < w && x + i < 30; i++)
        row[x + i] = i == 0 ? left : i == w - 1 ? right : mid;
}

static void fill_rect(int x, int y, int w, int h, u16 e) {
    for (int j = 0; j < h; j++) put_row(x, y + j, w, e, e, e);
}

// What an empty cell shows: nothing (0), or the cover pattern's tile for that cell.
static u16 blank(int x, int y) {
    if (!s_cover) return 0;
    if (s_cover->icon->width == 8) return (u16)(s_cover->base | (s_cover->bank << 12));
    return (u16)((s_cover->base + ((y & 1) << 1) + (x & 1)) | (s_cover->bank << 12));
}

static void clear_rect(int x, int y, int w, int h) {
    if (!s_cover) { fill_rect(x, y, w, h, 0); return; }
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) put(i, j, blank(i, j));
}

// Turn every empty cell into the cover (or, with no cover, every cover cell back to empty).
static void refill_blanks(const IconSlot *old) {
    int tiles = old ? (old->icon->width / 8) * (old->icon->height / 8) : 0;
    for (int y = 0; y < 20; y++) {
        u16 *row = &se_mem[SBB][y * 32];
        for (int x = 0; x < 30; x++) {
            int t = row[x] & 0x3FF;
            if (row[x] == 0 || (old && t >= old->base && t < old->base + tiles)) row[x] = blank(x, y);
        }
    }
}

static void load_font(const FontData *f) {
    if (s_font == f) return;
    s_font = f;
    s_font_bank = (u8)bg_pal_bank(f->palette);
    int n = f->glyph_count > 127 ? 127 : f->glyph_count;
    memcpy32(&tile_mem[0][T_FONT], f->tiles, n * 8);
}

// One row of 4bpp pixels with the transparent ones painted in `paper`. Branch-free: bit 0 of
// each nibble of `used` is set for a non-zero pixel, so the multiply drops `paper` into every
// transparent nibble at once (a pixel loop here made the first level-up menu skip frames).
static u32 on_paper(u32 w, u8 paper) {
    u32 used = (w | (w >> 1) | (w >> 2) | (w >> 3)) & 0x11111111u;
    return w | ((used ^ 0x11111111u) * paper);
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
    // Box frame: 2-pixel border with cut corners, paper inside. Built a row (8 pixels, one
    // word) at a time: pixel x is nibble x, so the left two pixels are the low byte.
    u32 all_paper = 0x11111111u * paper, all_border = 0x11111111u * border;
    for (int t = 0; t < 9; t++) {
        int left = t % 3 == 0, right = t % 3 == 2, top = t / 3 == 0, bottom = t / 3 == 2;
        u32 edges = (left ? 0x000000FFu : 0) | (right ? 0xFF000000u : 0);
        u32 *d = (u32 *)&tile_mem[0][T_FRAME + t];
        for (int y = 0; y < 8; y++) {
            u32 w = ((top && y < 2) || (bottom && y > 5)) ? all_border
                                                          : (all_paper & ~edges) | (all_border & edges);
            if ((left || right) && ((top && y == 0) || (bottom && y == 7)))
                w &= left ? ~0x0000000Fu : ~0xF0000000u;     // cut corner pixel
            d[y] = w;
        }
    }
}

// First tile of the bar fill levels in this color (made on first use; a HUD can show bars in
// up to BAR_SETS colors at once, e.g. a health bar that turns red).
static int bar_tiles(u8 color, u8 back) {
    for (int i = 0; i < s_bar_set_count; i++)
        if (s_bar_sets[i][0] == color && s_bar_sets[i][1] == back) return T_BAR + i * 9;
    int set = s_bar_set_count < BAR_SETS ? s_bar_set_count++ : BAR_SETS - 1;
    s_bar_sets[set][0] = color;
    s_bar_sets[set][1] = back;
    for (int level = 0; level <= 8; level++) {
        u32 *d = (u32 *)&tile_mem[0][T_BAR + set * 9 + level];
        for (int y = 0; y < 8; y++) {
            u32 w = 0x11111111u;            // a dark rim (font color 1) above and below, so the
            if (y >= 1 && y <= 6) {         // bar reads on any background
                w = 0;
                for (int x = 0; x < 8; x++) w |= (u32)(x < level ? color : back) << (4 * x);
            }
            d[y] = w;
        }
    }
    return T_BAR + set * 9;
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

// Map entry of a character at cell (x, y); a blank one shows the paper or the empty cell.
static u16 glyph(char c, int paper, int x, int y) {
    const FontData *f = paper ? s_paper_font : s_font;
    u8 g = f ? f->map[(u8)c & 127] : 0;
    if (!g) return paper ? (u16)((T_FRAME + 4) | (s_paper_bank << 12)) : blank(x, y);
    return paper ? (u16)((T_PAPER + g - 1) | (s_paper_bank << 12)) : (u16)((T_FONT + g - 1) | (s_font_bank << 12));
}

// ---- UI text: placeholders filled in from variables and species ----------------------
static int put_str(char *out, int n, int cap, const char *s) {
    while (s && *s && n < cap) out[n++] = *s++;
    return n;
}

static int put_num(char *out, int n, int cap, s32 v, int digits) {
    char buf[12];
    int k = 0, neg = v < 0;
    u32 u = neg ? (u32)-v : (u32)v;
    do { buf[k++] = (char)('0' + u % 10); u /= 10; } while ((u || k < digits) && k < 10);
    if (neg) buf[k++] = '-';
    while (k && n < cap) out[n++] = buf[--k];
    return n;
}

// The words a {var.field} placeholder prints for species number `index`.
static const char *species_field(s32 index, int field) {
    const BattleData *b = g_game.battle;
    if (!b || index < 0 || index >= b->species_count) return "";
    const SpeciesData *sp = b->species[index];
    if (field == 1) return sp->title;
    if (field == 2) return b->type_names[sp->type];
    if (field >= 3 && field <= 5 && sp->moves[field - 3]) return sp->moves[field - 3]->title;
    return "";
}

// Fill in a UI text's placeholders (see data.h): out gets plain characters (cap + 1 bytes).
// Returns its length.
static int expand(const char *s, char *out, int cap) {
    int n = 0;
    for (; *s && n < cap; s++) {
        char c = *s;
        if ((c == TXT_VAR || c == TXT_VAR02) && s[1]) {
            n = put_num(out, n, cap, vars_get((s16)((u8)s[1] - 1)), c == TXT_VAR02 ? 2 : 1);
            s++;
        } else if (c == TXT_SPECIES && s[1] && s[2]) {
            n = put_str(out, n, cap, species_field(vars_get((s16)((u8)s[1] - 1)), (u8)s[2]));
            s += 2;
        } else if (c == TXT_PICK && s[1] && s[2]) {
            s32 k = vars_get((s16)((u8)s[1] - 1));
            int count = (u8)s[2];
            if (k < 0) k = 0;
            if (k >= count) k = count - 1;
            s += 3;                                     // first choice
            for (int i = 0; i < count && *s; i++) {
                for (; *s && *s != TXT_PICKEND; s++)
                    if (i == k && n < cap) out[n++] = *s;
                if (*s) s++;                            // past the choice's end mark
            }
            s--;                                        // the loop steps onto what follows
        } else {
            out[n++] = c;
        }
    }
    out[n] = 0;
    return n;
}

// Draw plain text; \n starts a new line.
static void draw_plain(int x, int y, const char *s, int paper) {
    for (int cx = x; *s; s++) {
        if (*s == '\n') { y++; cx = x; continue; }
        put(cx, y, glyph(*s, paper, cx, y));
        cx++;
    }
}

// Draw UI text with its placeholders filled in.
static void draw_text(int x, int y, const char *s, int paper) {
    char buf[TEXT_MAX + 1];
    expand(s, buf, TEXT_MAX);
    draw_plain(x, y, buf, paper);
}

static void draw_box(const UiBox *b) {
    u16 bank = (u16)(s_paper_bank << 12);
    for (int j = 0; j < b->h; j++) {
        int t = T_FRAME + (j == 0 ? 0 : j == b->h - 1 ? 2 : 1) * 3;
        put_row(b->x, b->y + j, b->w, (u16)(t | bank), (u16)((t + 1) | bank), (u16)((t + 2) | bank));
    }
}

static void fill_paper(int x, int y, int w, int h) {
    fill_rect(x, y, w, h, (u16)((T_FRAME + 4) | (s_paper_bank << 12)));
}

// ---- HUD -----------------------------------------------------------------------------
#define HUD_SIG_MAX 32       // HUD elements that remember what they show (more are always redrawn)
static u32 s_hud_sig[HUD_SIG_MAX];

// 1 while a blinking HUD text is in its hidden half.
static int blink_off(const HudElement *e) {
    return e->blink && ((core_frame() / e->blink) & 1);
}

// A number that changes whenever what the element shows changes (the values it prints).
static u32 hud_signature(const HudElement *e) {
    if (e->kind == HUD_ICON) return 0;
    if (e->kind == HUD_ICON_REPEAT || e->kind == HUD_BAR)
        return (u32)vars_get(e->var) | (u32)vars_get(e->max_var) << 16;
    if (e->kind != HUD_TEXT) return (u32)vars_get(e->var);
    u32 sig = (u32)blink_off(e);
    for (const char *s = e->text; *s; s++) {
        if (*s == TXT_VAR || *s == TXT_VAR02 || *s == TXT_SPECIES || *s == TXT_PICK) {
            if (!s[1]) break;
            sig = sig * 31 + (u32)vars_get((s16)((u8)s[1] - 1));
            s++;
        }
    }
    return sig;
}

static void draw_hud_text(const HudElement *e) {
    char buf[TEXT_MAX + 1];
    int len = expand(e->text, buf, TEXT_MAX);
    int x = e->x, cx = e->x, cw = e->span;
    if (e->center) {
        x = e->x - len / 2;
        cx = e->x - e->span / 2 - 1;
        cw = e->span + 2;
    }
    clear_rect(cx, e->y, cw, 1);
    if (!blink_off(e)) draw_plain(x, e->y, buf, 0);
}

// Draw the HUD. all = 0 starts at the first element whose values changed: variables like the
// kill count change almost every frame in a horde, and a full redraw each time is wasted work.
// Everything after it is redrawn too, in order, so an element whose cleared area overlaps a
// later one still ends up exactly as a full redraw would leave it.
static void draw_hud(int all) {
    const HudData *h = s_hud;
    if (!h) return;
    load_font(h->font);
    int from = all ? 0 : h->count;
    for (int i = 0; i < h->count; i++) {
        u32 sig = i < HUD_SIG_MAX ? hud_signature(&h->elements[i]) : 0;
        if (i >= HUD_SIG_MAX || sig != s_hud_sig[i]) {
            if (i < from) from = i;
        }
        if (i < HUD_SIG_MAX) s_hud_sig[i] = sig;
    }
    for (int i = from; i < h->count; i++) {
        const HudElement *e = &h->elements[i];
        switch (e->kind) {
        case HUD_TEXT:
            draw_hud_text(e);
            break;
        case HUD_GAUGE: {
            s32 v = vars_get(e->var);
            v = v < 0 ? 0 : v >= e->icon_count ? e->icon_count - 1 : v;
            draw_icon(e->x, e->y, e->icons[v], 0xFF);
            break;
        }
        case HUD_ICON:
            draw_icon(e->x, e->y, e->icon, 0xFF);
            break;
        case HUD_ICON_REPEAT: {
            s32 v = vars_get(e->var);
            s32 slots = e->max_var >= 0 ? vars_get(e->max_var) : e->max;     // empty icons up to here
            int size = e->icon->width / 8;
            for (int k = 0; k < e->max; k++) {
                int x = e->x + k * e->spacing;
                if (k < v) draw_icon(x, e->y, e->icon, 0xFF);
                else if (e->empty_icon && k < slots) draw_icon(x, e->y, e->empty_icon, 0xFF);
                else clear_rect(x, e->y, size, size);
            }
            break;
        }
        case HUD_BAR: {
            s32 mx = e->max_var >= 0 ? vars_get(e->max_var) : e->max;
            if (mx <= 0) mx = 1;
            s32 v = vars_get(e->var);
            if (v < 0) v = 0;
            if (v > mx) v = mx;
            u8 color = e->color;                // low / mid colors when it runs low (health)
            if (e->low_color && v * 4 <= mx) color = e->low_color;
            else if (e->mid_color && v * 2 <= mx) color = e->mid_color;
            int base = bar_tiles(color, e->back);
            s32 px = v * e->length * 8 / mx;
            u8 bank = (u8)bg_pal_bank(h->font->palette);
            for (int k = 0; k < e->length; k++) {
                s32 lvl = px - k * 8;
                lvl = lvl < 0 ? 0 : lvl > 8 ? 8 : lvl;
                put(e->x + k, e->y, (u16)((base + lvl) | (bank << 12)));
            }
            break;
        }
        }
    }
    s_hud_version = vars_version();
}

void ui_show_hud(const HudData *h) {
    if (s_hud && s_hud != h && !s_menu && !s_dlg) {     // a different HUD: wipe the old one
        memset32(&se_mem[SBB][0], 0, 512);
        refill_blanks(NULL);
    }
    s_hud = h;
    s_hud_blinks = 0;
    for (int i = 0; i < h->count; i++)
        if (h->elements[i].kind == HUD_TEXT && h->elements[i].blink) s_hud_blinks = 1;
    if (!s_menu && !s_dlg) draw_hud(1);
}

// ---- upgrade menus: pick up to N random upgrades the player can take -----------------
#define UPG_ICON_DX 3       // the card icon sits 3 columns left of the text (2 wide + 1 gap)
#define UPG_TAG_DX  15      // "NEW!" / "LV2" / "EVOLVE!" column, right of the title

static s16 s_up[MAX_UPGRADE_CHOICES];
static u8 s_up_count;
EWRAM_BSS static s16 s_upg_nodes[NODE_COUNT];     // every upgrade node, found once (EWRAM: cold)
static s16 s_upg_total = -1;

static void upg_find_all(void) {
    if (s_upg_total >= 0) return;
    s_upg_total = 0;
    for (int n = 0; n < NODE_COUNT; n++)
        if (g_nodes[n].type == NT_UPGRADE) s_upg_nodes[s_upg_total++] = (s16)n;
}

static const UpgradeData *upg(s16 node) { return (const UpgradeData *)g_nodes[node].data; }
static s32 upg_level(const UpgradeData *u) { return u->var >= 0 ? vars_get(u->var) : 0; }

// What each upgrade (by its index in s_upg_nodes) can be offered as on the menu being opened.
enum { CLS_EVOLUTION, CLS_NORMAL, CLS_BONUS, CLS_NONE };
EWRAM_BSS static u8 s_upg_class[NODE_COUNT];

// Work out every upgrade's class once per menu: maxed weapons/items, incomplete evolution
// recipes and weapons replaced by an owned evolution can't be offered.
static void upg_classify(void) {
    for (int i = 0; i < s_upg_total; i++) {
        const UpgradeData *u = upg(s_upg_nodes[i]);
        u8 c = u->category == UPG_BONUS ? CLS_BONUS : u->category == UPG_EVOLUTION ? CLS_EVOLUTION : CLS_NORMAL;
        if (c != CLS_BONUS && upg_level(u) >= u->max_level) c = CLS_NONE;
        for (int r = 0; r < u->req_count && c != CLS_NONE && c != CLS_BONUS; r++)
            if (upg_level(upg(u->reqs[r].upgrade)) < u->reqs[r].level) c = CLS_NONE;
        s_upg_class[i] = c;
    }
    for (int k = 0; k < s_upg_total; k++) {
        const UpgradeData *e = upg(s_upg_nodes[k]);
        if (e->replaces < 0 || upg_level(e) <= 0) continue;
        for (int i = 0; i < s_upg_total; i++)
            if (s_upg_nodes[i] == e->replaces && s_upg_class[i] != CLS_BONUS) s_upg_class[i] = CLS_NONE;
    }
}

// Add random upgrades of one class until there are `want` choices (or none are left).
static void upg_pick_class(int cls, int want) {
    while (s_up_count < want) {
        int k = 0;
        for (int i = 0; i < s_upg_total; i++) k += s_upg_class[i] == cls;
        if (!k) return;
        int r = rng_range(0, k - 1);
        for (int i = 0; i < s_upg_total; i++)
            if (s_upg_class[i] == cls && r-- == 0) {
                s_up[s_up_count++] = s_upg_nodes[i];
                s_upg_class[i] = CLS_NONE;          // never twice on the same menu
                break;
            }
    }
}

// Evolutions first (they are rare and earned), then weapons/items, then bonus fillers.
static void upg_pick(int want) {
    if (want > MAX_UPGRADE_CHOICES) want = MAX_UPGRADE_CHOICES;
    upg_find_all();
    upg_classify();
    s_up_count = 0;
    for (int cls = CLS_EVOLUTION; cls <= CLS_BONUS; cls++) upg_pick_class(cls, want);
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
    if (d >= 0) draw_plain(slot->x, slot->y + 1, u->descriptions[d], 1);
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
    for (int i = 0; i < m->text_count; i++) draw_text(m->texts[i].x, m->texts[i].y, m->texts[i].text, paper);
    for (int i = 0; i < menu_count(); i++) {
        if (m->upgrade_count) draw_upgrade_card(i);
        else draw_text(m->options[i].x, m->options[i].y, m->options[i].label, paper);
    }
    draw_menu_cursor();
}

static void close_box(void) {
    memset32(&se_mem[SBB][0], 0, 512);
    refill_blanks(NULL);
    draw_hud(1);
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
    } else if (input_pressed(KEY_B | KEY_START) && m->on_cancel.count) {
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
        draw_plain(s_cx, s_cy, s_text_pos, 1);       // (long: no placeholders in dialogs)
        s_typing = 0;
    }
}

static void type_char(void) {
    const DialogLine *ln = &s_dlg->lines[s_line];
    char c = *s_text_pos++;
    if (c == '\n') { s_cy++; s_cx = ln->text_x; }
    else { put(s_cx, s_cy, glyph(c, 1, s_cx, s_cy)); s_cx++; }
    if (!*s_text_pos) s_typing = 0;
}

static void draw_choices(void) {
    const DialogData *d = s_dlg;
    for (int i = 0; i < d->choice_count; i++) {
        const MenuOption *o = &d->choices[i];
        put(o->x - 1, o->y, glyph(i == s_choice ? '>' : ' ', 1, o->x - 1, o->y));
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
    s_bar_set_count = 0;
    s_hud = NULL;
    s_hud_blinks = 0;
    s_menu = NULL;
    s_dlg = NULL;
    s_cover = NULL;
    s_cover_ticks = 0;
}

int ui_blocking(void) { return s_menu != NULL || s_dlg != NULL; }

// ---- cover -----------------------------------------------------------------------------
// Write the cover pattern's pixels into its VRAM tiles.
static void cover_upload(void) {
    int size = s_cover->icon->width;
    u32 *d = (u32 *)&tile_mem[0][s_cover->base];
    for (int t = 0; t < (size / 8) * (size / 8); t++) {
        int ox = (t % (size / 8)) * 8, oy = (t / (size / 8)) * 8;
        for (int y = 0; y < 8; y++) {
            u32 w = 0;
            for (int x = 0; x < 8; x++) w |= (u32)s_cover_px[(oy + y) * 16 + ox + x] << (4 * x);
            *d++ = w;
        }
    }
}

// The pattern blows sideways and a little down: shift it 2 pixels right and 1 down.
static void cover_drift(void) {
    int size = s_cover->icon->width;
    u8 old[16 * 16];
    memcpy(old, s_cover_px, sizeof(old));
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            s_cover_px[y * 16 + x] = old[((y + size - 1) % size) * 16 + (x + size - 2) % size];
    cover_upload();
}

void ui_cover(const IconData *icon, int ticks) {
    const IconSlot *old = s_cover;
    if (!icon || ticks <= 0) {
        s_cover = NULL;
        s_cover_ticks = 0;
        if (old) refill_blanks(old);
        return;
    }
    const IconSlot *slot = icon_slot(icon, 0xFF);
    if (!slot) return;
    // Unpack the icon's pixels (4 bits each, tiles in row-major order).
    int size = icon->width;
    for (int t = 0; t < (size / 8) * (size / 8); t++) {
        int ox = (t % (size / 8)) * 8, oy = (t / (size / 8)) * 8;
        for (int y = 0; y < 8; y++) {
            u32 w = icon->tiles[t * 8 + y];
            for (int x = 0; x < 8; x++) s_cover_px[(oy + y) * 16 + ox + x] = (u8)((w >> (4 * x)) & 15);
        }
    }
    s_cover = slot;
    s_cover_ticks = (u16)ticks;
    refill_blanks(old);
}

void ui_update(void) {
    if (s_dlg) dialog_update();
    else if (s_menu) menu_update();
    if (s_cover && !s_dlg && !s_menu) {
        if (--s_cover_ticks == 0) ui_cover(NULL, 0);
        else if ((core_frame() & 3) == 0) cover_drift();
    }
    if (s_hud && (s_hud_blinks || s_hud_version != vars_version()) && !s_dlg && !s_menu) draw_hud(0);
}
