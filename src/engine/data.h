// data.h - layout of the const node data that tools/codegen.py writes into generated/.
// Everything here lives in ROM. If you change a struct, change codegen.py in the same edit.
#ifndef ENGINE_DATA_H
#define ENGINE_DATA_H

#include <tonc_types.h>
#include "fixed.h"
#include "node_ids.h"

struct Actor;

// ---- logic ---------------------------------------------------------------------------
typedef struct Action Action;
typedef struct { const Action *items; u16 count; } ActionList;
typedef void (*ActionFn)(struct Actor *self);

struct Action {
    u8 op;              // ACT_* (catalog/actions.json order)
    u8 sub;             // if_var comparison (CMP_*) or spawn 'relative' flag
    s16 node;           // node argument (scene, sfx, music, dialog, menu, object), -1 = none
    s16 var;            // variable argument (VAR_*), -1 = none
    s16 var_from;       // set_var / add_var: use this variable's value instead of a, -1 = none
    s16 var_max;        // add_var: never go above this variable, -1 = none
    s16 pad;
    s32 a, b;           // number arguments (ticks, value, x/y, strength, percent)
    ActionList then_list, else_list;
    ActionFn fn;        // 'call' target
};

typedef struct { const s16 *xy; u16 count; } PointList;   // xy = x0,y0,x1,y1,...

typedef struct {
    void (*init)(struct Actor *self, const void *params);
    void (*update)(struct Actor *self, const void *params);
    void (*on_touch)(struct Actor *self, struct Actor *other, const void *params);
} BehaviorDef;

typedef struct { u8 behavior; const void *params; } LogicEntry;

// ---- art -----------------------------------------------------------------------------
typedef struct { u16 count; u16 colors[16]; } PaletteData;

typedef struct {
    const PaletteData *palette;
    u8 width, height, shape, size;      // pixels; OBJ shape/size bits
    u16 tiles_per_frame, frame_count;
    const u32 *tiles;                   // 4bpp, 8 words per tile, frames back to back
} SpriteData;

#define TILE_SOLID   1
#define TILE_ONE_WAY 2
#define TILE_HAZARD  4
#define TILE_LADDER  8

typedef struct {
    const PaletteData *palette;
    u16 tile_count;                     // includes the empty tile 0
    const u32 *tiles;
    const u8 *flags;                    // TILE_* per tile
} TilesetData;

typedef struct {
    const TilesetData *tileset;
    u8 layer, repeat_x;
    fixed parallax;
    u16 width, height;                  // tiles
    const u8 *cells;                    // tileset tile index per cell, row by row
} TilemapData;

typedef struct {
    const PaletteData *palette;
    u16 glyph_count;
    const u32 *tiles;
    const u8 *map;                      // [128] ASCII -> glyph number (1-based), 0 = blank
} FontData;

typedef struct {
    const PaletteData *palette;
    u8 width, height;
    const u32 *tiles;                   // row-major 8x8 tiles
} IconData;

// ---- audio ---------------------------------------------------------------------------
#define NOTE_HOLD 0xFF
#define NOTE_REST 0xFE

typedef struct { u8 value, ticks, volume, pad; } SfxStep;   // value: note, noise pitch or NOTE_REST
typedef struct { u8 channel, duty, priority, step_count; const SfxStep *steps; } SfxData;

typedef struct { u8 used, duty, volume, decay; u32 wave[4]; } Instrument;
typedef struct {
    fixed ticks_per_row;
    u8 loop;
    u16 row_count;
    Instrument inst[4];
    const u8 *rows[4];                  // per channel: note number, NOTE_HOLD or NOTE_REST; NULL = unused
} MusicData;

extern const u16 g_square_rate[128];    // note -> square channel frequency register
extern const u16 g_wave_rate[128];      // note -> wave channel frequency register

// ---- visual --------------------------------------------------------------------------
#define FLIP_X 1
#define FLIP_Y 2

typedef struct { u16 frame; u8 ticks, flip; } AnimFrame;
typedef struct { u16 at; ActionList actions; } AnimEvent;
typedef struct {
    const SpriteData *sprite;
    u8 loop, frame_count, event_count, pad;
    const AnimFrame *frames;
    const AnimEvent *events;
} AnimationData;

#define PTC_BURST  0
#define PTC_STREAM 1

typedef struct {
    const AnimationData *anim;
    u8 mode, count;
    u16 rate, lifetime;
    fixed speed_min, speed_max, gravity;
    s16 angle_min, angle_max;
} ParticleData;

// ---- world ---------------------------------------------------------------------------
typedef struct {
    s16 origin_x, origin_y;
    s16 hb_x, hb_y, hb_w, hb_h;         // hitbox relative to the origin
    u8 gravity, solid;
    u16 collides;                       // bit (1 << CAT_*) per category it collides with
    u8 default_slot;
    const AnimationData *anims[ANIM_SLOT_COUNT];
} BodyData;

typedef void (*ActorHook)(struct Actor *self);

typedef struct {
    u8 type, category;                  // NT_*, CAT_*
    const BodyData *body;               // NULL for triggers
    u16 zone_w, zone_h;                 // triggers only
    u8 logic_count;
    const LogicEntry *logic;
    s16 sounds[SND_COUNT];              // SND_* -> sfx node, -1 = none
    ActorHook on_start, on_update;      // custom code (src/game/<id>.c), may be NULL
} ActorData;

// ---- UI ------------------------------------------------------------------------------
typedef struct { u8 x, y, w, h, paper, border; } UiBox;    // tiles

enum { HUD_TEXT, HUD_ICON, HUD_ICON_REPEAT, HUD_BAR };

typedef struct {
    u8 kind, x, y, max;                 // x, y in tiles
    s16 var, max_var;                   // max_var: icon_repeat shows empty icons up to it (-1 = max)
    u8 length, color, back, spacing;
    const IconData *icon, *empty_icon;
    const char *text;                   // \001 + (var+1) = {var}; \002 + (var+1) = {var:02} (2 digits)
} HudElement;

typedef struct { const FontData *font; u8 count; const HudElement *elements; } HudData;

typedef struct { const char *label; u8 x, y; ActionList actions; } MenuOption;

typedef struct {
    const FontData *font;
    const char *title;
    u8 title_x, title_y, option_count, has_box;
    const MenuOption *options;
    const IconData *cursor;
    UiBox box;
    ActionList on_cancel;
    s16 sfx_move, sfx_select;
    u8 upgrade_count;                   // > 0: an upgrade menu; options only hold the card slots
} MenuData;

typedef struct {
    const char *speaker;
    const IconData *portrait;
    const char *text;                   // already word-wrapped with '\n'
    u8 speaker_x, speaker_y, portrait_x, portrait_y, text_x, text_y;
} DialogLine;

typedef struct {
    const FontData *font;
    UiBox box;
    u8 line_count, choice_count, ticks_per_char;
    const DialogLine *lines;
    const MenuOption *choices;
    ActionList on_end;
} DialogData;

// ---- upgrades (level-up choices) ---------------------------------------------------
enum { UPG_WEAPON, UPG_ITEM, UPG_EVOLUTION, UPG_BONUS };

typedef struct { s16 upgrade; u8 level, pad; } UpgradeReq;      // needs that upgrade at >= level

typedef struct {
    u8 category, max_level, req_count, desc_count;
    s16 var;                            // level variable, -1 = none (bonus)
    s16 replaces;                       // upgrade node it replaces (evolution), -1 = none
    const char *title;
    const IconData *icon;
    const char *const *descriptions;    // per level (index = current level), word-wrapped
    const UpgradeReq *reqs;
    ActionList on_pick;
} UpgradeData;

// ---- scenes and the game -------------------------------------------------------------
enum { INST_TILEMAP, INST_ACTOR, INST_HUD, INST_MENU, INST_DIALOG, INST_PARTICLE };

typedef struct {
    u8 kind, logic_count;
    s16 node;
    s16 x, y;
    const void *data;
    const LogicEntry *logic;            // actors: object logic, or a per-instance copy with overrides
} InstanceData;

typedef void (*SceneHook)(void);

#define CAM_BOUNDS_MAP  0
#define CAM_BOUNDS_NONE 1

typedef struct {
    u8 type, instance_count;
    s8 camera_follow;                   // instance index, -1 = none
    u8 camera_bounds;
    u16 backdrop;
    s16 music;                          // node, -1 = silence
    s16 cam_x, cam_y;
    const InstanceData *instances;
    ActionList on_start;
    SceneHook on_start_fn, on_update_fn;
} SceneData;

typedef struct { u8 type; const void *data; const char *id; } NodeEntry;

typedef struct {
    const char *title;
    s16 start_scene;
    u8 save;
    fixed gravity, max_fall;
} GameData;

extern const NodeEntry g_nodes[NODE_COUNT];
extern const GameData g_game;
extern const s32 g_var_initial[VAR_ARRAY_SIZE];
extern const u8 g_var_persistent[VAR_ARRAY_SIZE];  // 1 = kept by reset_vars (permanent unlocks)
extern const BehaviorDef g_behaviors[BHV_ARRAY_SIZE];

#endif
