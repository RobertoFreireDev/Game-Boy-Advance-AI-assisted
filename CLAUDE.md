# CLAUDE.md — AI-Built Game Boy Advance Games

This repository builds Game Boy Advance (GBA) games in C, written **100% by AI**.
The human never edits a file. The human describes what they want, reviews the result
in a read-only visualizer, and clicks `build.bat` / `run.bat`.

Read this whole file before every task. It is the contract for the node format, the
engine structure, the tools, and the visualizer.

**Creating a new game or replacing the current one?** Also read [`NEW_GAME.md`](NEW_GAME.md):
what context to load (and what to skip), which existing behaviors fit each genre, the workflow,
and how to play-test the ROM headlessly (Python + mGBA's GDB stub, never Lua).

---

## 1. Golden rules

1. **The human never changes anything manually.** You own every file: nodes, art, audio,
   C code, tools, `.bat` files, the visualizer and this `CLAUDE.md`. Never ask the human to
   edit a file, paste code or type commands. Their only actions are: approve the one-time
   prerequisite install (§3, done by the AI via `SETUP.md`), open the visualizer, click `build.bat`, click `run.bat`, and talk to you.
2. **Nodes are the source of truth.** All game content lives as JSON in `nodes/`. C code only
   reads data generated from nodes. Never hard-code content in C (positions, text, pixels,
   notes, speeds) that belongs in a node.
3. **Everything is text.** Art is pixel strings, audio is note strings, maps are character
   grids. No binary assets (PNG, WAV, MOD…) are ever added. Every asset stays readable,
   diffable, editable by AI and drawable by the visualizer.
4. **Generated files are never edited.** `generated/`, `build/`, `dist/` and
   `visualizer/data.js` are rebuilt by tools. Change the source, then regenerate.
5. **`nodes/nodes.json` is always in sync.** Every add, rename, move or delete of a node
   updates the index in the same change.
6. **Every node explains itself.** `name` and `notes` are mandatory, written in plain language
   for a non-programmer: what it is, where it is used, why it looks/behaves that way.
7. **Finish with green checks.** A task is not done until validation and build pass (§12).
8. **Keep the four mirrors in sync.** A node type exists in four places: this file (§6),
   `tools/validate.py`, `tools/codegen.py` and `visualizer/visualizer.html`. Adding or changing
   a type means updating all four in the same change. **Ask the human before** adding a new
   node type or changing the engine architecture.
9. **Respect the hardware.** Stay inside GBA limits (§9). If a request doesn't fit, say so and
   propose the closest thing that does.

---

## 2. Pipeline

### What the human does
1. Asks the AI to generate nodes:
   - **objects**: hud, icon, sfx, music, animation, particle, body, player, enemy, tree,
     moving platform, …
   - **scenes**: intro, menu, levels, inventory, …
2. Opens the visualizer (`view.bat`) and explores everything as a tree. **Read-only.**
3. Asks the AI for changes: nodes, assets (art, sprites, animations, sfx, music), code, anything.
4. Clicks `build.bat` to build the ROM.
5. Clicks `run.bat` to play it in the emulator.

### What happens underneath
```
nodes/**/*.json
   ├─ tools/validate.py ── checks every rule in this file (stops on error)
   ├─ tools/codegen.py ─── writes generated/*.c / *.h (const data, lives in ROM)
   ├─ tools/bundle.py ──── writes visualizer/data.js (everything the visualizer shows)
   └─ tools/build.py ───── runs the three above, compiles src/ + generated/ with devkitARM,
                           links libtonc, runs gbafix → dist/<rom_name>.gba
```

---

## 3. Toolchain and one-time setup

| Piece    | Choice                                   | Notes |
|----------|------------------------------------------|-------|
| OS       | Windows 10/11                            | Human entry points are `.bat` files. |
| Compiler | devkitPro → devkitARM (`gba-dev` group)  | Default install `C:\devkitPro`. |
| Library  | libtonc (ships with `gba-dev`)           | Low-level hardware access only; the engine is ours. |
| Language | C11                                      | No C++ unless the human asks. |
| Tools    | Python 3.10+, **standard library only**  | Never `pip install` anything. |
| Emulator | mGBA                                     | Launched by `run.bat`. |

**One-time setup** (the only install steps ever): devkitPro with GBA development, Python 3
(on PATH), mGBA. **The AI installs and verifies these by following [`SETUP.md`](SETUP.md)**
(exact commands, install paths, known pitfalls, smoke-test ROM). The human only accepts UAC
prompts. Read `SETUP.md` before installing tools or when a build reports a missing tool, and
keep it up to date when you learn something new about the toolchain.

Build details (owned by `tools/build.py`, no Makefile, no MSYS shell, so it works from a double-click):
- Calls `arm-none-eabi-gcc` directly. Reference flags:
  `-mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi -O2 -std=c11 -Wall -Wextra -ffunction-sections -fdata-sections`
- **Hot code**: a source named `*.iwram.c` is compiled as ARM (`-marm -mlong-calls`) and the linker
  script places it in IWRAM (fast, 32-bit). Only for code a measurement proved too slow (§8).
- Links with `-specs=gba.specs -ltonc -Wl,--gc-sections`, then `objcopy -O binary`, then `gbafix`.
- Only recompiles changed files (gcc `-MMD` dependency files in `build/obj/`).
- Runs gcc **from the repo root with relative paths**, so the accents/spaces in the repo path
  never reach gcc. libtonc is passed explicitly: `-I <dkp>/libtonc/include`, `-L <dkp>/libtonc/lib`.
- Include paths: `-Isrc -Igenerated` (so code writes `#include "engine/actor.h"`, `"node_ids.h"`).
- **Any compiler or linker warning fails the build** (zero-warnings rule, §8).
- Path resolution: `tools/config.json` → env `DEVKITPRO` (map `/opt/devkitpro` to `C:\devkitPro`)
  → `C:\devkitPro`. Emulator: `tools/config.json` → common install folders → `PATH`.
- If anything is missing, print one short friendly message saying exactly what to install and
  from where. The human will paste it to you.

---

## 4. Folder structure

```
.
├── CLAUDE.md                ← this file (AI-maintained)
├── SETUP.md                 ← install + verify the toolchain (AI-maintained)
├── NEW_GAME.md              ← playbook for creating/replacing a game + testing (AI-maintained)
├── .gitignore               ← generated/, build/, dist/, visualizer/data.js
├── build.bat                ← human: build the ROM
├── run.bat                  ← human: play the ROM in mGBA
├── view.bat                 ← human: refresh data and open the visualizer
├── nodes/
│   ├── nodes.json           ← index of every node + game settings
│   ├── scenes/<id>.json     ← one file per scene
│   └── objects/<type>/<id>.json   ← one file per object, one folder per type
├── catalog/
│   ├── behaviors.json       ← every behavior: description + params (§7)
│   └── actions.json         ← every action: description + params (§7)
├── src/
│   ├── main.c               ← boots the engine, nothing else
│   ├── engine/              ← generic, game-agnostic engine (§8); data.h = layout of generated data
│   ├── behaviors/           ← one .c/.h per behavior
│   └── game/                ← game-specific code, only when nodes can't express it
├── tools/
│   ├── validate.py  codegen.py  bundle.py  build.py  run.py
│   ├── common.py            ← shared: node-type registry, loading, text layout (wrap, boxes)
│   └── config.json          ← local paths (devkitPro, emulator)
├── visualizer/
│   ├── visualizer.html      ← single file: HTML + CSS + JS inline, no dependencies
│   └── data.js              ← GENERATED by bundle.py
├── generated/               ← GENERATED C data
├── build/                   ← GENERATED object files
└── dist/                    ← GENERATED ROM
```

---

## 5. The node system

### 5.1 Concepts
- A **node** is one JSON file describing one thing. Two kinds:
  - **scene** — a screen of the game (intro, title, menu, level, inventory, game over…).
    **Scenes are always root nodes.** Never a child, never nested.
  - **object** — anything reusable: art, audio, animations, particles, bodies, actors, UI.
- A scene **places** objects through `instances` (position + optional overrides). An instance
  is not a node; it is a line inside the scene that points at an object node.
- An object can **own** other objects through `children`. **Nest only when the child exists
  solely for that parent** — e.g. a `body` owns its `animation`s and its `sprite` sheet; a
  `player` owns its `body`. If a second node needs the same thing, it is not a child: make it
  a root node and reference it.
- Any node can **reference** any other node by id (`"music": "mus_level_1"`). A reference is
  not nesting.

Mental model for humans: scenes at the top, the things placed in them, and each thing
broken into the parts it is made of.

### 5.2 `nodes/nodes.json`
```json
{
  "format": 1,
  "game": {
    "title": "HERO QUEST",
    "game_code": "AHQE",
    "rom_name": "hero_quest",
    "start_scene": "scn_intro",
    "save": false,
    "gravity": 0.25,
    "max_fall_speed": 4,
    "variables": [
      { "name": "coins",   "type": "int",  "initial": 0,     "notes": "Coins collected." },
      { "name": "health",  "type": "int",  "initial": 3,     "notes": "Hearts left." },
      { "name": "has_key", "type": "flag", "initial": false, "notes": "Found the castle key." }
    ]
  },
  "nodes": [
    { "id": "scn_intro",      "kind": "scene",  "type": "intro",     "path": "scenes/scn_intro.json",                 "parent": null },
    { "id": "plr_hero",       "kind": "object", "type": "player",    "path": "objects/player/plr_hero.json",          "parent": null },
    { "id": "body_hero",      "kind": "object", "type": "body",      "path": "objects/body/body_hero.json",           "parent": "plr_hero" },
    { "id": "anim_hero_run",  "kind": "object", "type": "animation", "path": "objects/animation/anim_hero_run.json",  "parent": "body_hero" }
  ]
}
```
`title` ≤ 12 chars, `game_code` exactly 4 chars (ROM header). `gravity` (px/tick², default 0.25)
and `max_fall_speed` (px/tick, default 4) apply to every body with `physics.gravity`. Variables
are the game's global state; HUD, behaviors and actions read and write them by name. Flags are
stored as 0/1. A variable with `"persistent": true` keeps its value through `reset_vars` (permanent
unlocks, banked gold); `save_game` saves every variable. Index order: scenes first, then objects.

### 5.3 Common fields (every node file)

| Field      | Required | Meaning |
|------------|----------|---------|
| `id`       | yes | Unique snake_case id with the type prefix (§6). Equals the file name. |
| `kind`     | yes | `scene` or `object`. |
| `type`     | yes | One of the types in §6. |
| `name`     | yes | Human-friendly name. |
| `notes`    | yes | Plain-language explanation for the human. |
| `tags`     | no  | Free labels for filtering in the visualizer. |
| `children` | no  | Ordered list of owned child ids. Objects only; never contains a scene. |
| `code`     | no  | Path to custom C in `src/game/` when nodes can't express the logic (§7.3). |

Structural rules (enforced by `validate.py`):
- Path is `scenes/<id>.json` or `objects/<type>/<id>.json`.
- A node has at most one parent; no cycles; child types must be allowed by the "May own" column in §6.
- `children` in the parent file and `parent` in `nodes.json` must agree.
- Every reference must exist and have the expected type. No orphan files, no missing files.

### 5.4 Units and text formats
- **Position**: integer pixels. x → right, y → down, (0,0) = top-left of the scene.
- **Time**: ticks. 1 tick = 1 frame ≈ 1/60 s.
- **Speed**: pixels per tick, decimals allowed (codegen converts to fixed-point 24.8).
- **Angle**: degrees, 0 = right, 90 = up.
- **Color**: `"#RRGGBB"`. Codegen and visualizer both quantize to 15-bit BGR555.
- **Pixels**: array of strings, one string per row, one char per pixel. `.` = transparent
  (palette index 0), `1`–`9`, `a`–`f` = palette index 1–15. All rows the same length.
- **Notes (audio)**: `C2`…`B7` (C2/B2 only for wave bass), sharps as `C#5`. `--` = hold, `..` = silence, `|` = bar line
  (ignored, readability only). Tokens separated by spaces. Noise channel uses `X0`…`Xf` (noise pitch).
- **Tile maps**: one char per 8×8 tile, legend defined in the tileset. `.` is always the empty tile.
- **UI positions** (HUD elements, menu layout, boxes) are pixels but must be multiples of 8:
  BG0 text sits on the 8×8 tile grid.

---

## 6. Node types

### 6.1 Catalog

| Type | Prefix | What it is | Key fields | May own |
|------|--------|------------|-----------|---------|
| **Scenes** | | | | |
| `intro` `title` `menu` `level` `inventory` `cutscene` `game_over` `credits` | `scn_` | A screen of the game. Same structure for all; the type sets defaults and the visualizer icon. | `backdrop`, `music`, `camera`, `instances[]`, `on_start[]` | — (root only) |
| **Art** | | | | |
| `palette` | `pal_` | Up to 16 colors. Index 0 = transparent. | `colors[]` | — |
| `sprite` | `spr_` | Sprite sheet for moving things (OBJ layer). | `palette`, `width`, `height` (valid OBJ size, §9), `frames{name: pixels}` | — |
| `tileset` | `ts_` | 8×8 background tiles keyed by one char. | `palette`, `tiles{char: {pixels, solid, one_way, hazard, ladder}}` | — |
| `tilemap` | `map_` | A background layer drawn with a tileset. | `tileset`, `layer` (1–3), `rows[]`, `parallax` (1 = moves with camera) | `tileset` |
| `font` | `font_` | 8×8 glyphs for text. | `palette`, `glyphs{char: pixels}` | — |
| `icon` | `icon_` | Small single UI image (8×8 or 16×16). | `palette`, `pixels` | — |
| **Audio** | | | | |
| `sfx` | `sfx_` | Short sound effect on one PSG channel. | `channel`, `duty`, `steps[{note│pitch, ticks, volume}]`, `priority` | — |
| `music` | `mus_` | Looping song on the 4 PSG channels. | `bpm`, `rows_per_beat`, `loop`, `instruments{}`, `patterns{}`, `order[]` | — |
| **Visual** | | | | |
| `animation` | `anim_` | Frames from one sprite sheet over time. | `sprite`, `loop`, `frames[{frame, ticks, flip_x, flip_y}]`, `events[{at, action}]` | — |
| `particle` | `ptc_` | Particle emitter (dust, sparks, splash). | `animation`, `mode` (burst/stream), `count`, `rate`, `lifetime`, `speed[min,max]`, `angle[min,max]`, `gravity` | `animation` |
| **Things in the world** | | | | |
| `body` | `body_` | What an actor looks like + its collision shape. | `origin[x,y]`, `hitbox{x,y,w,h}` (relative to origin), `physics{gravity, solid, collides_with[]}`, `animations{slot: id}`, `default_animation` | `sprite`, `animation`, `particle` |
| `player` | `plr_` | Controlled by the human. | `body`, `logic[]`, `sounds{}` | `body`, `particle`, `sfx` |
| `enemy` | `enm_` | Hostile actor. | same as player | same |
| `npc` | `npc_` | Friendly actor, usually talks. | same | same |
| `prop` | `prop_` | Static or decorative thing (tree, rock, door), a weapon's shot, or an invisible logic-only thing (no `body`: a run director). | same (`body` optional) | same |
| `platform` | `plat_` | Moving / falling platform. | same | same |
| `pickup` | `item_` | Collectible: coin, key, heart. | same | same |
| `trigger` | `trg_` | Invisible zone that runs actions (exit, checkpoint). | `zone{w,h}`, `logic[]` | — |
| **UI** | | | | |
| `hud` | `hud_` | Always-on overlay bound to variables. | `font`, `elements[{kind: text│icon│icon_repeat│bar, x, y, …}]`; text uses `{var}` placeholders | `icon` |
| `menu` | `menu_` | List of options the player picks from. | `font`, `title`, `options[{label, actions[]}]`, `cursor` (icon), `layout`, `on_cancel[]` | `icon` |
| `dialog` | `dlg_` | Text box conversation. | `font`, `box{x,y,w,h}`, `lines[{speaker, portrait, text}]`, `choices[{label, actions[]}]`, `on_end[]` | `icon` |
| **Progression** | | | | |
| `upgrade` | `upg_` | One card of the level-up menu: a weapon, a passive item, an evolution or a bonus filler. | `category`, `title`, `icon`, `var`, `max_level`, `descriptions[]`, `requires[]`, `replaces`, `on_pick[]` | `icon` |

Actor types (`player` … `pickup`) share one structure; the type gives sensible defaults and
groups them for humans. **What an actor does comes only from its `logic` list.**

### 6.1b Field details (what `validate.py` enforces)

- **scene**: `backdrop` color; `music` id or `null` (silence); `camera {follow: instance id,
  bounds: "map"|"none", x, y}` (x, y = start when not following); `instances[{id, object, x, y,
  overrides}]` — `object` may be a tilemap, actor, hud, menu, dialog or particle (a placed particle
  is a permanent emitter); at most one HUD and one menu-or-dialog; one tilemap per layer.
- **palette**: `colors[0]` is the transparent slot (its value is ignored).
- **sprite** frames are listed in order; the art faces **right** (the engine mirrors it).
- **tileset** keys are one printable ASCII char (not `.` or space); flags `solid`, `one_way`,
  `hazard`, `ladder` (a tile can't be both solid and one-way; `ladder` + `one_way` = ladder top).
- **tilemap**: layer 1 is the collision layer and must have `parallax` 1; `repeat_x: true` makes a
  background layer wrap sideways. The map sides are invisible walls; falling below the map kills.
- **font**: glyph keys are ASCII 32–126; lowercase falls back to uppercase; space needs no glyph.
- **sfx** steps: `note` (or `".."` silence) for square/wave, `pitch` 0–15 for noise.
- **animation** `events[{at: frame index, action | actions}]` run when that frame starts.
- **particle**: positions are the sprite's center; `rate` = ticks between stream particles.
- **body** animation slots: `idle walk run jump fall hurt die attack climb`, plus the top-down
  slots `idle_up idle_down walk_up walk_down attack_up attack_down`. `origin` mirrors with
  the art when it faces left; the **hitbox does not mirror**. The `*_up` / `*_down` slots are
  never mirrored (their art is drawn as seen from behind / from the front). `physics.collides_with` categories:
  `tiles player enemy npc prop platform pickup trigger`. Two actors touch (and both get
  `on_touch`) when either lists the other's category. `physics.solid` = others can't walk through
  it and can stand on it (like a crate).
- **actor** `sounds{event: sfx}` events: `jump land hurt die collect stomp talk attack`.
- **trigger**: `zone{w,h}` with its top-left corner at the instance x, y; touches the player only.
- **hud** elements (x, y multiples of 8): `text {text}` (`{var}` placeholders, 3 chars reserved
  per value; `{var:02}` pads to 2 digits, for clocks), `icon {icon}`, `icon_repeat {icon, empty_icon?, var, max, spacing?}`,
  `bar {var, max, length (tiles), color, back}` (colors are indexes of the font palette).
- **menu**: `layout {x, y, spacing, title_y}`, optional `box {x, y, w, h, paper, border}`,
  `sounds {move, select}`. Up/Down move, A picks, B or START runs `on_cancel`. Picking an option **locks**
  the menu (no more input) and runs its actions, so they should `close_menu`, `goto_scene`,
  `open_menu` or `show_dialog` (the validator warns otherwise). Option labels and the title may use
  `{var}` / `{var:02}` placeholders (re-`open_menu` to refresh them, e.g. a shop).
- **upgrade menu**: a menu with `"upgrades": 1-3` instead of `options` (a `box` is required). Each time
  it opens the engine picks that many random cards from every `upgrade` node: evolutions whose
  recipe is complete first, then weapons/items below `max_level`, then `bonus` fillers. Card layout
  (tiles): text at `layout.x`, the 16x16 icon 3 columns left of it, the cursor left of the icon;
  title row with a NEW!/LV2/EVOLVE! tag 15 columns right, then 2 description rows of 22 chars;
  `layout.spacing` ≥ 24 px. Picking a card adds 1 to its `var`, zeroes the weapon it `replaces`,
  closes the menu and runs `on_pick`. Nothing to offer = the menu does not open.
- **upgrade**: `category` weapon / item / evolution / bonus. `title` ≤ 14 chars; `icon` 16x16 (use the
  menu font's palette so the box shows behind it). `var` holds the level (0 = not owned; not used by
  bonus). `max_level` 1-9 (evolution: 1; bonus: unlimited, no max_level). `descriptions`: one per
  level for weapons/items (index = current level, so [0] is the NEW card), one for the others; each
  fits 2 lines of 22 chars. Evolutions need `requires[{upgrade, level (1-9 or "max")}]` and
  `replaces` (a weapon). The level variable is what weapon behaviors read (`weapon.level_var`).
- **dialog**: `box {x, y, w, h, paper, border}` (required), `ticks_per_char` (0 = instant).
  Codegen word-wraps each line (`common.wrap_text`); the speaker uses the first row, a 16×16
  portrait takes 3 columns. A/B skip typing / next line; choices show after the last line.
  An icon on a box (portrait, cursor) only gets the box color behind it when it uses the font's
  palette; in any other palette its transparent pixels show the map, so paint its background with
  a palette color equal to the box paper.
- A menu or dialog **pauses the world** (actor logic, physics, animations) until it closes.
- Use **one font per scene** for the HUD and box-less menus (BG0 has one transparent-font slot).

### 6.2 Examples

**Sprite** (8×8 coin, 2 frames):
```json
{
  "id": "spr_coin", "kind": "object", "type": "sprite",
  "name": "Coin", "notes": "Gold coin. Front view and edge view for the spin.",
  "palette": "pal_items", "width": 8, "height": 8,
  "frames": {
    "front": ["..1111..", ".122221.", "12233221", "12322321", "12322321", "12233221", ".122221.", "..1111.."],
    "edge":  ["...11...", "...12...", "...12...", "...12...", "...12...", "...12...", "...12...", "...11..."]
  }
}
```

**Animation:**
```json
{
  "id": "anim_coin_spin", "kind": "object", "type": "animation",
  "name": "Coin spin", "notes": "Loops forever while the coin sits in the level.",
  "sprite": "spr_coin", "loop": true,
  "frames": [
    { "frame": "front", "ticks": 10 },
    { "frame": "edge",  "ticks": 6 },
    { "frame": "front", "ticks": 10, "flip_x": true },
    { "frame": "edge",  "ticks": 6 }
  ],
  "events": []
}
```

**Body** (owns its sprite, animations and dust particles):
```json
{
  "id": "body_hero", "kind": "object", "type": "body",
  "name": "Hero body",
  "notes": "How the hero looks and the box used for collisions. The hitbox is narrower than the art so jumps feel fair.",
  "origin": [8, 16],
  "hitbox": { "x": -5, "y": -14, "w": 10, "h": 14 },
  "physics": { "gravity": true, "solid": true, "collides_with": ["tiles", "platform", "enemy", "pickup", "trigger"] },
  "animations": { "idle": "anim_hero_idle", "run": "anim_hero_run", "jump": "anim_hero_jump", "hurt": "anim_hero_hurt" },
  "default_animation": "idle",
  "children": ["spr_hero", "anim_hero_idle", "anim_hero_run", "anim_hero_jump", "anim_hero_hurt", "ptc_hero_dust"]
}
```
`origin` is the point in the art (from its top-left) that sits on the instance's x,y — usually the feet.

**Player:**
```json
{
  "id": "plr_hero", "kind": "object", "type": "player",
  "name": "Hero", "notes": "The character the human controls. Runs, jumps, has 3 hearts.",
  "body": "body_hero",
  "logic": [
    { "behavior": "platformer_controller", "params": { "speed": 1.5, "jump_height": 40, "jump_button": "A" } },
    { "behavior": "health", "params": { "var": "health", "invincible_ticks": 60,
      "on_death": [{ "do": "goto_scene", "scene": "scn_game_over" }] } },
    { "behavior": "camera_target", "params": {} }
  ],
  "sounds": { "jump": "sfx_jump", "hurt": "sfx_hurt" },
  "children": ["body_hero"]
}
```

**Tilemap** (rows use the tileset's chars):
```json
{
  "id": "map_level_1", "kind": "object", "type": "tilemap",
  "name": "Level 1 ground", "notes": "Main layer the hero walks on. '#' is solid ground, '=' is a one-way ledge.",
  "tileset": "ts_grass", "layer": 1, "parallax": 1,
  "rows": [
    "................................",
    "..........====..................",
    "################....############"
  ]
}
```

**Scene:**
```json
{
  "id": "scn_level_1", "kind": "scene", "type": "level",
  "name": "Level 1 – Green Hills",
  "notes": "First level. Teaches running and jumping over small gaps. Ends at the flag.",
  "backdrop": "#78c8f8",
  "music": "mus_level_1",
  "camera": { "follow": "player", "bounds": "map" },
  "instances": [
    { "id": "map",    "object": "map_level_1" },
    { "id": "player", "object": "plr_hero",  "x": 24,  "y": 112 },
    { "id": "tree_1", "object": "prop_tree", "x": 96,  "y": 112 },
    { "id": "lift_1", "object": "plat_lift", "x": 160, "y": 104,
      "overrides": { "follow_path": { "points": [[0, 0], [0, -48]] } } },
    { "id": "coin_1", "object": "item_coin", "x": 168, "y": 40 },
    { "id": "exit",   "object": "trg_exit",  "x": 480, "y": 96,
      "overrides": { "trigger_zone": { "on_enter": [{ "do": "goto_scene", "scene": "scn_level_2" }] } } },
    { "id": "hud",    "object": "hud_main" }
  ],
  "on_start": [{ "do": "fade_in", "ticks": 20 }]
}
```
`overrides` are keyed by behavior name and replace only the listed params for that one instance.
Instance ids are unique inside the scene; `camera.follow` uses an instance id.

**SFX:**
```json
{
  "id": "sfx_jump", "kind": "object", "type": "sfx",
  "name": "Jump", "notes": "Short rising blip when the hero jumps.",
  "channel": "square1", "duty": 2, "priority": 1,
  "steps": [
    { "note": "C5", "ticks": 2, "volume": 12 },
    { "note": "E5", "ticks": 2, "volume": 10 },
    { "note": "G5", "ticks": 4, "volume": 7 }
  ]
}
```
`channel`: `square1` `square2` `wave` `noise`. `duty`: 0=12.5% 1=25% 2=50% 3=75%.
`volume`: 0–15 (wave channel: 0–4). Noise steps use `"pitch": 0–15` instead of `note`.

**Music:**
```json
{
  "id": "mus_level_1", "kind": "object", "type": "music",
  "name": "Green Hills theme", "notes": "Cheerful loop. Melody on square1, bass on wave, light drums on noise.",
  "bpm": 132, "rows_per_beat": 4, "loop": true,
  "instruments": {
    "square1": { "duty": 2, "volume": 10, "decay": 2 },
    "square2": { "duty": 1, "volume": 7,  "decay": 3 },
    "wave":    { "wave": "0123456789abcdeffedcba9876543210", "volume": 4 },
    "noise":   { "volume": 6, "decay": 1 }
  },
  "patterns": {
    "A": {
      "square1": "C5 -- E5 -- G5 -- E5 -- | D5 -- F5 -- A5 -- F5 --",
      "wave":    "C3 -- -- -- C3 -- -- -- | D3 -- -- -- D3 -- -- --",
      "noise":   "X2 .. X8 .. X2 .. X8 .. | X2 .. X8 .. X2 X2 X8 .."
    }
  },
  "order": ["A", "A"]
}
```
Every channel in a pattern has the same number of rows. Missing channels are silent.
`wave` is 32 hex nibbles (one 4-bit waveform). SFX borrow a channel; music resumes after.

---

## 7. Logic: behaviors, actions, custom code

### 7.1 Behaviors (what objects do)
- A behavior is a reusable logic module: `src/behaviors/<name>.c/.h` + an entry in
  `catalog/behaviors.json` (plain description, params with type, default and description).
- Objects enable behaviors in `logic[]`; scenes can override params per instance.
- Interface: `init(actor, params)`, `update(actor, params)`, optional `on_touch(actor, other, params)`.
- Param types: `int`, `fixed`, `bool`, `ticks`, `button`, `var`, `node:<type>` (`node:actor` = any actor
  type), `actions`, `points`, `choice` (one of `options`), `string`. Each param has a `default` or
  `required: true`.
- A logic list may use the same behavior more than once (several `weapon`s, several `spawn_wave`s);
  a scene instance cannot override a behavior its object lists more than once.
- Codegen turns the catalog into one C struct per behavior (`Params_<name>` in
  `generated/behavior_params.h`, choices as `<BEHAVIOR>_<PARAM>_<OPTION>` defines) and registers
  the `bhv_<name>_init/update/on_touch` functions it finds in the `.c` file. Behaviors keep
  private per-actor state in `bhv_state(actor)` (`BSTATE_WORDS` words each).
- Movement behaviors pick body animation slots by convention: `idle`, `walk`, `run`, `jump`,
  `fall`, `hurt`, `die`, `attack`, `climb` (a missing `run` uses `walk` and back, `fall` uses
  `jump`, `die` uses `hurt`, then `default_animation`).
- **Facing (top-down)**: every actor has a `facing` (right / left / up / down, starts down) set by
  `actor_face()`. When it faces up or down, asking for `idle`, `walk`/`run` or `attack` plays the
  matching `*_up` / `*_down` slot if the body has it (a missing `*_up` / `*_down` slot falls back
  to the plain one). `facing_left` keeps the last horizontal direction and mirrors the side art.
- `health` on a body **without gravity** (top-down) knocks straight away from the hit (slowing
  down) and dies in place; with gravity it knocks sideways and falls off the screen.
- Starter set: `platformer_controller`, `topdown_controller`, `patrol`, `chase_player`,
  `follow_path`, `solid_platform`, `health`, `damage_on_touch`, `stompable`, `collectible`,
  `trigger_zone`, `talk`, `camera_target`, `spawn_particles`, `button_actions` (a button press runs
  actions, e.g. START opens a pause menu). Top-down set: `sword_attack`,
  `wander`, `locked_door`. Survivors set: `weapon` (auto-fire, level from a variable), `projectile`
  (the shot), `aura`, `swarm` (horde movement with grid-based crowd spreading), `magnet` (pickups fly
  to the player), `spawn_wave` (timed off-screen waves), `run_clock`, `level_up`. Add more as games need them.
- Prefer a new reusable behavior over custom code.

### 7.2 Actions (what happens when something occurs)
Action lists appear in `on_start`, `on_enter`, `on_death`, menu options, dialog choices,
animation events, etc. Format: `{ "do": "<action>", ...params }`, run in order; `wait` pauses
the list. Catalog in `catalog/actions.json`. Starter set:
`goto_scene`, `fade_in`, `fade_out`, `wait`, `play_sfx`, `play_music`, `stop_music`,
`set_var`, `add_var` (both take `value` or `from` another variable; `add_var` can cap at `max_var`),
`reset_vars` (skips persistent variables), `if_var` (`then[]`/`else[]`), `if_chance` (`percent`,
`then[]`/`else[]`, random drops), `show_dialog`, `open_menu`,
`close_menu`, `spawn`, `destroy_self`, `shake_camera`, `save_game`, `load_game`, `call` (custom C, §7.3).
Each catalog entry has a `sentence` the visualizer reads aloud ("Go to scene {scene}").
Codegen maps every action to the generic `Action` struct (`src/engine/data.h`); a new action
needs a case in `codegen.py` (`Gen.action`) and in `src/engine/actions.c`.

### 7.3 Custom code (last resort)
When something truly can't be expressed with behaviors and actions, set `"code":
"src/game/<id>.c"` on the node (scenes and actors only). Codegen registers the hooks it finds:
scenes `void <id>_on_start(void)` / `void <id>_on_update(void)`, actors
`void <id>_on_start(Actor *self)` / `void <id>_on_update(Actor *self)`. The `call` action runs any
`void name(Actor *self)` defined in `src/game/*.c`.
Explain in the node's `notes` what the code does, so the visualizer shows it in plain words.

---

## 8. Engine structure (C)

Designed so a human can follow "what runs when" without reading code.

```
src/engine/
├── data.h         layout of every generated const struct (must match codegen.py)
├── config.h       pool sizes and limits (MAX_ACTORS, MAX_PARTICLES, …) — validate.py reads them
├── core.c/.h      main loop, VBlank wait, frame counter
├── input.c/.h     pressed / held / released for every button
├── fixed.h        fixed-point 24.8 math, sin/cos table, rng
├── vars.c/.h      game variables (get / set / add by id)
├── scene.c/.h     load / unload scenes, spawn instances, transitions and fades
├── actor.iwram.c/.h   actor pool (in EWRAM), runs each actor's logic list every tick
├── physics.iwram.c/.h gravity, tile collision (solid/one_way/hazard/ladder), actor overlaps
│                      (contacts only test the categories an actor lists, via per-frame buckets;
│                       big buckets many actors look into are binned in a 32 px grid)
├── anim.iwram.c/.h    animation players and frame events
├── particles.c/.h particle pool
├── camera.c/.h    follow target, clamp to bounds, shake
├── bg.c/.h        tiles/palettes upload, tilemap scrolling, streaming for big maps
├── sprites.iwram.c/.h OAM shadow buffer and OBJ VRAM allocation
├── audio.c/.h     PSG driver: music sequencer + sfx
├── ui.c/.h        text, hud, menus (incl. upgrade cards), dialogs
├── actions.c/.h   action list interpreter
└── save.c/.h      SRAM save/load of variables (only if game.save)
```

**Every frame:**
1. Wait for VBlank → copy OAM shadow, scroll registers, palette fades
2. `input_update`
3. `scene_update` → each actor runs its `logic` in instance order
4. `physics_update` → move, collide, fire `on_touch`
5. `anim_update`, `particles_update`
6. `camera_update`
7. `ui_update` (hud, menu, dialog)
8. `audio_update`
9. Build the OAM shadow for the next VBlank

Steps 3 (actor logic), 4 and 5 are skipped while a menu or dialog is open; action lists, fades,
camera, UI and audio keep running. A scene switch (`goto_scene`) happens at the start of the next
frame's `scene_update`; the new scene's `on_start` runs in that same frame (so `fade_in` never
flashes).

**VRAM plan** (mode 0, 4bpp): BG0 = UI on charblock 0 / screenblock 31; tilemap layer *n* (1–3)
= charblock *n* / screenblock 31−*n*, priority *n*; big maps stream into the 32×32 hardware map as
the camera moves. Sprites use priority 1 (above every map, below the UI); players draw on top.
Palette banks are handed out per scene in first-use order (BG and OBJ separately).

**Code rules:**
- The engine is game-agnostic: it never mentions a specific node id. Game-specific code lives in `src/game/`.
- No `malloc`. Fixed pools sized in `config.h`.
- No `float`/`double`. Fixed-point only.
- Node data is `const` and lives in ROM. Runtime state lives in RAM structs.
- Every node gets an enum `NODE_<ID_IN_CAPS>` in `generated/node_ids.h`; variables get `VAR_<NAME>`;
  node data is `node_<id>` in `generated/game_data.c`.
- One module, one job. Each public function has a one-line comment in plain English.
- Move hot code to IWRAM (ARM mode) only when a measured slowdown requires it: rename the file
  `*.iwram.c` (done for actor, physics, anim, sprites and the `swarm`, `projectile` and `health`
  behaviors after profiling hordes of 60+ monsters). IWRAM is 32 KB shared with globals and the
  stack (~27 KB used); keep it lean.
- Thumb code (everything not `*.iwram.c`) has no fast divide or 64-bit multiply: in code that
  runs per actor per tick avoid `/`, `%` by a variable and `fx_mul`, or move it to IWRAM.
- Big pools live in EWRAM (`EWRAM_BSS`, not zeroed at boot: clear them yourself).
- Zero warnings in our code.

---

## 9. GBA limits (validator enforces what it can)

- **Screen** 240×160, ~60 fps. All work for a frame must fit in that frame.
- **Video mode 0**, 4 tiled BG layers. Default plan: BG0 = UI (hud/menu/dialog text),
  BG1 = main tilemap (collision), BG2 = background, BG3 = far background.
- **Sprites (OBJ)**: max 128 on screen, per-scanline limits apply. Valid sizes:
  8×8, 16×16, 32×32, 64×64, 16×8, 32×8, 32×16, 64×32, 8×16, 8×32, 16×32, 32×64.
  OBJ VRAM 32 KB = 1024 4bpp tiles.
- **Palettes**: 16 BG + 16 OBJ palettes, 16 colors each (4bpp), 15-bit color, index 0 transparent.
- **BG VRAM** 64 KB shared by tiles and maps. Regular BGs max 512×512 px; bigger maps are
  streamed by `bg.c`. One char per tile limits a tileset to ~90 tiles.
- **Memory**: 32 KB IWRAM (fast), 256 KB EWRAM, ROM up to 32 MB.
- **Audio**: 4 PSG channels (square1 with sweep, square2, wave, noise). Direct Sound is not used in v1.
- **Input**: D-pad, A, B, L, R, START, SELECT.
- **Text**: 8×8 font → 30 chars per line. Validator checks dialog/menu/hud text fits its box.

---

## 10. AI work visualizer

### 10.1 Files and loading
- `visualizer/visualizer.html` — the whole app in **one file** (HTML, CSS, JS inline).
  No frameworks, no CDN, no internet. Works by double-clicking (`file://`).
- `visualizer/data.js` — generated by `tools/bundle.py`:
  `window.GAME_DATA = { generated_at, game, index, nodes: {id: json}, catalog: {behaviors, actions}, validation: {ok, errors[], warnings[]}, engine: {...}, derived: {dialogs, menus} }`.
  `derived` holds the layouts codegen computes (dialog word-wrap, menu positions) so the
  visualizer draws exactly what the GBA draws.
  Loaded with `<script src="data.js">` because `fetch` doesn't work on `file://`.
- `bundle.py` runs after every AI change, so a browser refresh shows the latest work.
- Missing `data.js` → friendly message "Run view.bat". Validation errors → red banner listing
  them; the visualizer still shows everything it can.

### 10.2 Read-only, always
- No editing UI of any kind: no fields that change data, no drag-to-move, no save, no export of changes.
- Controls only change *how you look*: select, search, filter, expand/collapse, zoom,
  play/pause/step, overlays (grid, hitboxes, origins, labels, camera frame), volume.
- Header hint: "Read-only — ask the AI to change anything." Every node shows its id and file
  path with a "Copy id" button so the human can name it when asking for changes.

### 10.3 Layout
```
┌──────────────────┬────────────────────────────────┬───────────────────┐
│ LEFT SIDEBAR     │ MAIN AREA                      │ RIGHT SIDEBAR     │
│ 1. Nodes (list)  │ preview of the selected node   │ properties, notes │
│ 2. Tree          │                                │                   │
└──────────────────┴────────────────────────────────┴───────────────────┘
```
**Header**: game title, last generated time, validation status, node counts.

**Left sidebar**
- *Section 1 — Nodes*: flat list of every node, search box, filter chips by kind and type,
  each row = type icon + name + id.
- *Section 2 — Tree*: two root groups. **Scenes** → each scene expands to its instances
  (instance id → object name; click selects the object). **Objects** → root objects expand to
  their children, recursively. The selected node is highlighted in both sections and the tree
  auto-expands to it.

**Right sidebar**
- Name, id, kind/type, file path.
- **Notes** at the top, prominent.
- **Properties**: every field, made readable — colors as swatches, references as clickable links,
  actions as plain sentences, behaviors with their catalog description and params
  (marking defaults vs. per-instance overrides).
- **Children** and **Used by** (every node/scene that references this node).
- Custom code path if `code` is set.
- Raw JSON (collapsible).

**Main area — what each type shows**

| Type | Preview |
|------|---------|
| scene | Whole scene on a canvas: backdrop, tilemaps with parallax, every instance drawn with its default animation (animated), HUD on top. Overlays: tile grid, hitboxes, origins, instance labels, platform paths, trigger zones, 240×160 screen frame at camera start. Click an instance → selects its object. Instance list below. |
| palette | Swatches with index, hex and 15-bit value. |
| sprite, icon, font | Pixel-perfect zoom of every frame/glyph; optional index grid. |
| tileset | Every tile with its char and flags (solid, one-way, hazard, ladder). |
| tilemap | Full map render with collision overlay. |
| animation | Live playback at real speed, frame strip with ticks, play/pause/step, flips, events on a timeline. |
| particle | Live emitter simulation with restart button. |
| body | Animation slots as tabs, live playback with hitbox and origin overlay, physics summary. |
| player, enemy, npc, prop, platform, pickup, trigger | Body preview, hitbox, **logic list** (each behavior: plain description + params), sounds with play buttons, path preview for `follow_path`, zone for triggers. |
| hud, menu, dialog | Rendered inside a 240×160 GBA screen with the real font and icons. HUD uses variables' initial values; dialog has prev/next line; menu shows the cursor on each option (upgrade menus show sample cards). |
| upgrade | Its card on the level-up menu with prev/next level, the text per level as wrapped on the GBA, the evolution recipe and what picking it does. |
| sfx, music | **Audio player**: play/stop/loop + visual (step list for sfx; tracker grid per channel with playhead for music). Web Audio synth approximating the PSG: square with duty, 4-bit wave, noise, 0–15 volume. |

### 10.4 Fidelity and navigation
- Quantize every color to 15-bit before drawing. Integer zoom, `image-rendering: pixelated`.
- Same timing (1 tick = 1/60 s), origin, hitbox and flip rules as the engine. If they ever
  disagree, the engine is right — fix the visualizer.
- Deep links: `visualizer.html#scn_level_1` selects that node.
- Keyboard: ↑/↓ moves through the list, Space = play/pause.

---

## 11. The `.bat` files

All three: `@echo off`, `cd /d "%~dp0"`, find Python (`py -3`, then `python`), call one
Python script, print a clear ✔ / ✖ result, `pause` at the end, non-zero exit code on failure.
The AI runs the Python scripts directly; the `.bat` files exist for the human.

- `build.bat` → `tools\build.py` (validate → codegen → bundle → compile → `dist\<rom_name>.gba`)
- `run.bat` → `tools\run.py` (builds first if the ROM is missing or older than any source;
  launches mGBA only if the build succeeded)
- `view.bat` → `tools\bundle.py`, then `start "" "visualizer\visualizer.html"`

---

## 12. How the AI handles every request

1. Read `nodes/nodes.json` and the nodes involved. Read code only when needed.
   For a new game (or a new genre), follow [`NEW_GAME.md`](NEW_GAME.md) for what to load and in what order.
2. If the request is ambiguous in a way that changes the result, ask **one** short question.
   Otherwise choose sensibly and say what you chose.
3. Make the change: nodes first; catalog / behaviors / engine code only if needed.
4. Update `nodes.json`. If you change something shared (a palette, a font, a behavior), list what else it affects.
5. Run `python tools/validate.py` and fix everything.
6. Run `python tools/build.py` and fix every error and warning.
7. Make sure `visualizer/data.js` is regenerated.
8. If git is set up, commit with a clear message so "undo the last change" is easy.
9. Report to the human in plain language: what you made/changed (name + id), what to click
    in the visualizer, what to try in the game (controls), anything you couldn't do and why.
    No code or JSON in the report unless asked.

**Done means:** validation passes, the ROM builds, `data.js` is fresh, report sent.

---

## 13. Art and audio style

- **Pixel art**: 4–8 colors per sprite, dark outline, one light source (top-left), readable
  silhouette at 1×, consistent style across the game. Animations 2–6 frames, 4–10 ticks each.
- **Palettes**: share palettes across related objects to save palette slots.
- **SFX**: short (≤ 30 ticks), distinct per event.
- **Music**: melody on square1, harmony on square2, bass on wave, drums on noise; melodies in C3–C7;
  build songs from short patterns reused in `order`.