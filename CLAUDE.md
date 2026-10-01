# CLAUDE.md — AI-Generated Game Boy Advance Games

This file is the single source of truth for how Claude builds and maintains this project.
Read it fully before every task. If a request conflicts with this file, ask the human first.

---

## 1. Core principles

1. **The human never edits anything manually.** Every file in this repo (code, JSON, assets, scripts,
   this file) is created and changed by the AI. The human only: asks for changes, looks at the
   visualizer, double-clicks `build.bat` / `run.bat` / `view.bat`.
2. **Everything is a node.** The game is described as JSON nodes (objects and scenes). C code
   implements the generic engine and reusable behaviors; game content lives in nodes.
3. **Readable over clever.** Structure, names and JSON must be easy for a non-programmer to read in
   the visualizer and to describe in a request ("make the player jump higher", "add a tree to level 2").
4. **Text-only assets.** Art, palettes, maps, music and sfx are stored as human-readable text inside
   node JSON. No binary assets are hand-made. The generator converts them to C data at build time.
5. **Always leave the project building.** After any change, run the generator check and the build.
   Never finish a task with a broken build.

---

## 2. Target & toolchain

| Item | Choice |
|---|---|
| Platform | Game Boy Advance (240×160, 60 fps) |
| Language | C11 for engine and behaviors (C++ not used, to keep one style) |
| Compiler | devkitPro **devkitARM** (`gba-dev` package group) |
| Library | **libtonc** (registers, OAM, interrupts, BIOS calls) |
| Header fix | `gbafix` (from devkitPro) |
| Generator | Python 3 — `tools/gen.py` (no third-party packages) |
| Emulator | **mGBA** |
| OS | Windows (`.bat` entry points) |
| Editor | VS Code (human only reads; AI writes) |

One-time setup (the only manual step): install devkitPro with `gba-dev`, Python 3, and mGBA.
Paths are stored in `config.bat`; the AI edits it if paths differ.

---

## 3. Repository layout

```
/CLAUDE.md                  ← this file
/nodes.json                 ← registry of ALL nodes (the index)
/nodes/
  scenes/                   ← one JSON file per scene node
  objects/
    art/                    ← palette, sprite, tileset, tilemap, font, icon
    audio/                  ← sfx, music
    fx/                     ← particle (and shared animations, rare)
    entities/               ← player, enemy, npc, prop, platform, pickup, trigger
    ui/                     ← hud, dialog
/src/
  engine/                   ← generic runtime (never game-specific)
  behaviors/                ← reusable logic modules, one .c per behavior
    behaviors.json          ← registry: name, description, params (shown in visualizer)
/tools/
  gen.py                    ← validate nodes + generate C data + visualizer data
  visualizer/
    index.html              ← read-only visualizer (HTML + CSS + JS in ONE file)
    data.js                 ← GENERATED snapshot of all nodes (never edit)
/build/                     ← GENERATED (never edit): gen/*.c/*.h, objects, game.gba, build.log
/Makefile
/config.bat                 ← tool paths (DEVKITPRO, MGBA, PYTHON)
/build.bat                  ← human: build the ROM
/run.bat                    ← human: run the ROM in mGBA
/view.bat                   ← human: regenerate data.js and open the visualizer
```

Rules:
- `build/` and `tools/visualizer/data.js` are always generated. Never edit them; regenerate them.
- Game-specific logic never goes in `src/engine/`. If a feature is reusable, it's a behavior.

---

## 4. Node system

### 4.1 Two kinds of node

| Kind | What it is | Nesting |
|---|---|---|
| `scene` | A screen of the game: intro, menu, level, inventory, game over… | **Always root-level. Never nested.** |
| `object` | Anything else: art, audio, animation, body, particle, entity, HUD, dialog… | Root-level by default; nested only when owned (see 4.4) |

### 4.2 `nodes.json` — the registry

Every root-level node has exactly one entry and exactly one file. Nested children are NOT listed
(they live inside their parent's file).

```json
{
  "schema": 1,
  "game": {
    "title": "MY GAME",
    "gameCode": "AMGE",
    "startScene": "scene.intro"
  },
  "nodes": [
    { "id": "scene.intro",   "kind": "scene",  "type": "intro",  "file": "nodes/scenes/intro.json" },
    { "id": "scene.level_1", "kind": "scene",  "type": "level",  "file": "nodes/scenes/level_1.json" },
    { "id": "pal.player",    "kind": "object", "type": "palette","file": "nodes/objects/art/pal_player.json" },
    { "id": "spr.player",    "kind": "object", "type": "sprite", "file": "nodes/objects/art/spr_player.json" },
    { "id": "ent.player",    "kind": "object", "type": "player", "file": "nodes/objects/entities/player.json" },
    { "id": "sfx.jump",      "kind": "object", "type": "sfx",    "file": "nodes/objects/audio/sfx_jump.json" }
  ]
}
```

### 4.3 Common fields (every node file)

```json
{
  "id": "ent.player",
  "kind": "object",
  "type": "player",
  "name": "Player",
  "notes": "Free text for humans. Explain intent, not implementation.",
  "tags": ["hero"],
  "props": { },
  "children": [ ]
}
```

- `id`: unique, lowercase, `<prefix>.<snake_name>`. Prefixes: `scene` `pal` `spr` `tiles` `map`
  `font` `icon` `sfx` `mus` `anim` `body` `fx` `ent` `hud` `dlg`.
- Nested child ids are `parentId/localName`, e.g. `ent.player/body`, `ent.player/body/walk`.
- References to other nodes are always by `id` string, in fields ending in `Ref` or `Refs`
  (e.g. `"paletteRef": "pal.player"`). The generator validates every reference.
- `notes` is mandatory and must be kept up to date when the node changes.

### 4.4 When to nest

Nest a child inside its parent **only** when it is owned by that parent and never reused:
- `body` → nested `animation` list ✅
- `hud` → nested `widget` list ✅
- `dialog` → nested `page` list ✅
- `music` / `sfx` / `palette` / `sprite` / `particle` → **root-level**, referenced by id (they are reused)

If a nested child later needs to be shared, promote it to a root-level node, register it in
`nodes.json`, and replace the nested copy with a `Ref`.

### 4.5 Object types

| Type | Purpose | Key fields |
|---|---|---|
| `palette` | 16 colors (4bpp). Index 0 is transparent. | `colors[16]` as `"#RRGGBB"` |
| `sprite` | Sprite sheet: frames of pixel art | `paletteRef`, `size` (`"16x16"`), `frames{name: rows[]}` |
| `icon` | Small single image for HUD/inventory | same as sprite, one frame |
| `tileset` | 8×8 background tiles | `paletteRef`, `tiles{name: rows[]}` |
| `tilemap` | Background layer + collision | `tilesetRef`, `legend`, `rows[]`, `collisionLegend`, `collision[]` |
| `font` | 8×8 glyphs for HUD/dialog text | `paletteRef`, `glyphs{char: rows[]}` |
| `animation` | Frame sequence (usually nested in body) | `frames[]`, `fps`, `loop` |
| `body` | Visual + physical shape of an entity | `spriteRef`, `origin`, `hitboxes[]`, `children: [animation…]`, `defaultAnim` |
| `particle` | Particle emitter | `spriteRef`, `frame`, `count`, `lifetime`, `speed`, `spread`, `gravity` |
| `sfx` | Short sound effect (PSG) | `channel`, `priority`, `steps[]` |
| `music` | Looping music (PSG) | `bpm`, `rowsPerBeat`, `instruments`, `tracks{sq1,sq2,wave,noise}` |
| `player` `enemy` `npc` `prop` `platform` `pickup` `trigger` | **Entities.** All share ONE generic runtime struct; the type is a label for humans. | `bodyRef` or nested `body`, `behaviors[]`, `layer`, `sfxRefs`, `particleRefs` |
| `hud` | On-screen overlay | `fontRef`, `children: [widget…]` (text, icon, bar, counter) |
| `dialog` | Text box conversation | `fontRef`, `box`, `children: [page…]` |

To add a new object type: first add it to this table (with key fields), then to `gen.py` validation,
then to the visualizer renderer. Never invent an undocumented type.

### 4.6 Text pixel format

Pixel rows are strings; each character is a palette index `0-9A-F` (`0` = transparent).
Width and height must be multiples of 8. GBA sprite sizes only: 8×8, 16×16, 32×32, 64×64,
16×8, 32×8, 32×16, 64×32, 8×16, 8×32, 16×32, 32×64.

```json
{
  "id": "spr.player",
  "kind": "object",
  "type": "sprite",
  "name": "Player sprite sheet",
  "notes": "Hero, blue shirt. Facing right; engine flips for left.",
  "paletteRef": "pal.player",
  "size": "16x16",
  "frames": {
    "idle_0": [
      "0000011111100000",
      "0000122222210000",
      "...14 more rows..."
    ]
  }
}
```

### 4.7 Entity example (with nested body + animations)

```json
{
  "id": "ent.player",
  "kind": "object",
  "type": "player",
  "name": "Player",
  "notes": "Main character. Runs, jumps, collects coins.",
  "layer": "main",
  "children": [
    {
      "id": "ent.player/body",
      "kind": "object",
      "type": "body",
      "name": "Player body",
      "notes": "Hitbox is narrower than the sprite so edges feel fair.",
      "spriteRef": "spr.player",
      "origin": [8, 16],
      "hitboxes": [ { "name": "hurt", "x": -5, "y": -14, "w": 10, "h": 14 } ],
      "defaultAnim": "idle",
      "children": [
        { "id": "ent.player/body/idle", "kind": "object", "type": "animation", "name": "Idle",
          "notes": "Breathing loop.", "frames": ["idle_0", "idle_1"], "fps": 4, "loop": true },
        { "id": "ent.player/body/walk", "kind": "object", "type": "animation", "name": "Walk",
          "notes": "4-frame run cycle.", "frames": ["walk_0", "walk_1", "walk_2", "walk_3"], "fps": 10, "loop": true }
      ]
    }
  ],
  "behaviors": [
    { "use": "platformer_move", "params": { "speed": 1.5, "accel": 0.25 } },
    { "use": "platformer_jump", "params": { "jumpSpeed": 4.0, "gravity": 0.25 } },
    { "use": "camera_follow",   "params": { "deadzoneW": 32, "deadzoneH": 24 } }
  ],
  "sfxRefs": { "jump": "sfx.jump" },
  "particleRefs": { "land": "fx.dust" }
}
```

### 4.8 Scene example

```json
{
  "id": "scene.level_1",
  "kind": "scene",
  "type": "level",
  "name": "Level 1 — Forest",
  "notes": "Tutorial level. Teaches jumping before the first enemy.",
  "background": {
    "bg3": { "mapRef": "map.forest_sky",  "scroll": [0.25, 0] },
    "bg2": { "mapRef": "map.level_1_main", "scroll": [1, 1] }
  },
  "musicRef": "mus.forest",
  "hudRef": "hud.game",
  "camera": { "boundsFromMap": "map.level_1_main" },
  "instances": [
    { "name": "player",   "ref": "ent.player",   "x": 24,  "y": 120 },
    { "name": "tree_1",   "ref": "ent.tree",     "x": 80,  "y": 120 },
    { "name": "slime_1",  "ref": "ent.slime",    "x": 200, "y": 120,
      "overrides": { "patrol": { "range": 48 } } },
    { "name": "lift_1",   "ref": "ent.platform_lift", "x": 300, "y": 100 }
  ],
  "behaviors": [
    { "use": "scene_goal_exit", "params": { "x": 600, "nextScene": "scene.level_2" } }
  ]
}
```

- `instances` place entities; `overrides` change behavior params for that instance only.
- Scene logic (menus, transitions, win/lose) is also behaviors, prefixed `scene_`.

### 4.9 Audio format (PSG channels only)

Music and sfx use the 4 GB-compatible PSG channels (`sq1`, `sq2`, `wave`, `noise`). No sample playback
(Direct Sound) in schema 1 — it keeps audio text-editable and previewable in the browser.

```json
{
  "id": "mus.forest",
  "kind": "object",
  "type": "music",
  "name": "Forest theme",
  "notes": "Calm, loops every 8 bars.",
  "bpm": 120,
  "rowsPerBeat": 4,
  "instruments": {
    "lead": { "channel": "sq1", "duty": 2, "volume": 12, "envelope": -1 },
    "bass": { "channel": "wave", "waveform": "triangle", "volume": 2 },
    "hat":  { "channel": "noise", "volume": 6, "envelope": -3 }
  },
  "tracks": {
    "sq1":   "lead: C5 4, E5 4, G5 8, R 4, G5 4, E5 8",
    "wave":  "bass: C3 8, G2 8, A2 8, F2 8",
    "noise": "hat: X 2, R 2, X 2, R 2"
  },
  "loop": true
}
```

Track syntax: `instrument: NOTE LENGTH, NOTE LENGTH, …` — length in rows, `R` = rest, `X` = noise hit.
`sfx` uses the same syntax in a single `steps` string plus `channel` and `priority`.
When an sfx plays, it borrows its channel from the music and returns it when done.

---

## 5. Behaviors (logic)

All game logic is reusable C modules in `src/behaviors/`. Entities and scenes enable them by name.

Registry `src/behaviors/behaviors.json` (the visualizer shows this text to the human):

```json
{
  "platformer_jump": {
    "description": "Press A to jump when on ground. Holding A longer jumps higher.",
    "appliesTo": ["player"],
    "params": {
      "jumpSpeed": { "type": "fixed", "default": 4.0, "desc": "Initial upward speed (px/frame)" },
      "gravity":   { "type": "fixed", "default": 0.25, "desc": "Downward accel (px/frame²)" }
    },
    "file": "src/behaviors/platformer_jump.c"
  }
}
```

Each behavior file implements:

```c
void bhv_platformer_jump_init(Obj *o, const BhvParams *p);
void bhv_platformer_jump_update(Obj *o, const BhvParams *p);
// optional:
void bhv_platformer_jump_on_hit(Obj *o, Obj *other, const BhvParams *p);
```

- `gen.py` generates the dispatch table and named param accessors
  (e.g. `PLATFORMER_JUMP_JUMPSPEED(p)`) from `behaviors.json`.
- Param types: `int`, `fixed` (converted to 24.8), `bool`, `nodeRef` (converted to node enum).
- Per-behavior runtime state lives in the object's fixed `state[16]` byte slot for that behavior.
- Max 4 behaviors per entity. A behavior must not know about any specific node id; it receives
  everything through params.
- Prefer composing existing behaviors over writing new ones. Before creating a behavior, check
  the registry for one that already does the job.

---

## 6. Engine structure (`src/engine/`)

Each module is one `.c` + `.h` pair with one clear job:

| Module | Job |
|---|---|
| `main.c` | Init hardware, run the 60 fps loop: input → scene update → objects update → render → audio, wait VBlank |
| `fixed.h` | 24.8 fixed-point type `fx32` and math macros (**no floats anywhere**) |
| `input.c` | Button state: held / pressed / released |
| `scene.c` | Load/unload scenes, spawn instances, transitions (fade), scene behaviors |
| `obj.c` | Fixed pool of `Obj` (max 64), spawn/destroy, behavior dispatch |
| `body.c` | Animation playback, flip, hitbox world positions |
| `collide.c` | AABB vs AABB, AABB vs tilemap collision layer |
| `render.c` | BG layers, camera scroll, OAM shadow buffer (128 sprites), VRAM tile allocation |
| `particles.c` | Small particle pool (max 32) using OAM |
| `hud.c` | HUD widgets drawn on BG0 |
| `dialog.c` | Text box on BG0, paging with A |
| `audio.c` | PSG music sequencer + sfx channel borrowing, ticked once per frame |
| `save.c` | SRAM save/load (only if a node requests saving) |

Layer convention: **BG0** = HUD/dialog text, **BG1** = foreground, **BG2** = main level,
**BG3** = parallax background. Display mode 0, 4bpp tiles everywhere.

Generated data (`build/gen/`): `nodes.h` (enum of all node ids), `assets.c` (tiles, palettes, maps,
audio as `const` arrays in ROM), `scenes.c`, `entities.c`, `behaviors_table.c`.

---

## 7. Hardware budgets (gen.py must report these)

| Resource | Limit | Notes |
|---|---|---|
| Screen | 240×160 | Visualizer shows this as the camera frame |
| OAM sprites | 128 on screen | engine + particles share it |
| Sprite tiles | 1024 × 4bpp (32 KB) | frames are streamed per entity type when a scene loads |
| BG palettes / OBJ palettes | 16 + 16, 16 colors each | |
| BG layers | 4 | see layer convention |
| IWRAM | 32 KB | hot code + stack |
| EWRAM | 256 KB | object pool, buffers |
| ROM | aim < 4 MB | |

If a request exceeds a budget, explain it to the human in plain words and propose an alternative.

---

## 8. Read-only visualizer (`tools/visualizer/index.html`)

**One file** containing HTML, CSS and JS. Vanilla JS, no libraries, no network, no build step.
It loads `data.js` (generated by `gen.py`: `window.GAME_DATA = {...}`) via a `<script>` tag so it
works by double-clicking (no local server, no `fetch`).

**Read-only for everything.** There are no edit buttons, no inputs that change data, no save.
View controls are allowed (select, zoom, play/pause, toggle overlays). If `data.js` is missing,
show: "Run view.bat to generate the data."

### Layout

```
┌──────────────┬──────────────────────────────┬──────────────────┐
│ LEFT SIDEBAR │          MAIN AREA           │  RIGHT SIDEBAR   │
│ 1. Nodes     │  renders the selected node   │  properties      │
│ 2. Tree      │                              │  notes           │
│              │                              │  references      │
└──────────────┴──────────────────────────────┴──────────────────┘
```

### Left sidebar
- **Section 1 — Nodes:** flat list of all root nodes grouped by kind → type, with a search box.
- **Section 2 — Tree:** hierarchy: Game → Scenes → instances → entity → nested children
  (body → animations, hud → widgets, dialog → pages). A final group "Unused" lists nodes no scene
  references. Clicking any item selects it everywhere.

### Main area (by selected node)
- **Scene:** draws BG layers and every instance (default animation, first frame) at integer zoom,
  camera frame rectangle (240×160), toggles for grid / hitboxes / collision layer. Clicking an
  instance selects it.
- **Sfx / music:** audio player (play/stop, loop) synthesized with WebAudio from the PSG data
  (square with duty, wave, noise), channel mute toggles, and a simple per-channel note timeline.
  Label it "approximate preview".
- **Entity / object:** all animations playing side by side with names and fps, hitbox overlays,
  particle preview loop, the list of enabled behaviors as cards (description + params, overrides
  highlighted), linked sfx/particles.
- **HUD:** renders the widgets at their screen positions over a 240×160 frame.
- **Dialog:** renders the box and lets the human step through pages (view only).
- **Palette / sprite / tileset / tilemap / font / icon:** swatches, frame grid, tile grid, map render, glyph grid.

### Right sidebar
- All properties of the selected node (nested objects collapsible), the `notes` text prominently,
  file path, **Uses** (refs out) and **Used by** (refs in), and any `gen.py` warnings for that node.

---

## 9. Generator (`tools/gen.py`)

Commands:
- `python tools/gen.py --check` → validate only (schema, unique ids, every ref resolves, files exist,
  pixel sizes, palette indices, sprite sizes, behavior names/params, scene nesting rule, budgets).
- `python tools/gen.py` → validate, then write `build/gen/*` and `tools/visualizer/data.js`.

Errors must be in plain language with the node id and file, e.g.
`ERROR scene.level_1 (nodes/scenes/level_1.json): instance "slime_1" refers to "ent.slim" which does not exist.`

---

## 10. Human pipeline

1. **Ask AI to generate nodes** — objects (hud, icon, sfx, music, animation, particle, body, player,
   enemy, tree, movable platform…) and scenes (intro, menu, levels, inventory…).
2. **Look at them** — double-click `view.bat`; browse the tree in the visualizer (read-only).
3. **Ask AI for changes** — nodes, assets (art, sprites, animations, sfx, music), code, anything.
4. **Build** — double-click `build.bat`.
5. **Play** — double-click `run.bat` (opens the ROM in mGBA).

If a build fails, the human says "build failed"; the AI reads `build/build.log` and fixes it.

### Batch file contracts
- `config.bat` — sets `DEVKITPRO`, `DEVKITARM`, `MGBA`, `PYTHON`. Only file with machine paths.
- `build.bat` — `call config.bat` → `gen.py` → `make` → `gbafix` → `build/game.gba`. All output also
  written to `build/build.log`. Prints `BUILD OK` or `BUILD FAILED` and pauses on failure.
- `run.bat` — calls `build.bat` if `build/game.gba` is missing, then starts mGBA with the ROM.
- `view.bat` — runs `gen.py` and opens `tools/visualizer/index.html` in the default browser.

---

## 11. AI workflow for every request

1. Read `nodes.json` and the relevant node files before changing anything.
2. Restate the request in node terms if it's ambiguous ("I'll add a `jump` animation to
   `ent.player/body` and a `sfx.jump` node") — ask only when a choice really changes the game.
3. Make the change: JSON first, then behaviors/engine code if needed.
4. Keep `nodes.json` in sync (add/remove/rename entries; update every `Ref` on rename).
5. Update `notes` on every node you touched.
6. Run `python tools/gen.py --check`, then the full build. Fix all errors and warnings.
7. Reply to the human with a short summary in node terms: nodes added / changed / removed, and
   what to look at in the visualizer or in game. No code dumps unless asked.

Never:
- edit `build/` or `data.js` by hand,
- put game-specific ids or numbers inside `src/engine/`,
- use floats, `malloc` during gameplay, or libraries outside devkitARM/libtonc,
- create binary asset files by hand,
- nest a scene, or nest a reusable object,
- add a node type or field that isn't documented here (update this file first).

---

## 12. First task: scaffold (if the repo is empty)

1. Create the folder layout, `config.bat`, `build.bat`, `run.bat`, `view.bat`, `Makefile`
   (devkitARM `gba_rules`, sources `src/engine src/behaviors build/gen`, link `-ltonc`).
2. Write `tools/gen.py` with full validation and generation.
3. Write the engine modules listed in section 6 (minimal but working).
4. Write `tools/visualizer/index.html` per section 8.
5. Create a sample game: `pal.*`, `spr.player`, `font.default`, `ent.player` (move + jump),
   `ent.tree` (prop), `scene.intro` (title + "PRESS START"), `scene.level_1`, `sfx.jump`, `mus.title`.
6. Verify: `gen.py --check` passes, `build.bat` produces `build/game.gba`, the visualizer shows every node.
