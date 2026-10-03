# Changelog

One entry per request, newest first. Written for humans: what changed and which nodes.

## 2026-10-03 — New-game playbook for the AI

**Request:** write down what the AI learned while making Night Swarm, so the next new game starts
faster and is tested the right way.

**Added:** `NEW_GAME.md` — which files the AI reads first (and which it can skip), which existing
behaviors fit each kind of game, the step-by-step workflow, and how to play-test the ROM with
Python and the emulator's debugger (never Lua). `CLAUDE.md` now points to it. No nodes changed.

## 2026-10-03 — Replace Sun Shrine with a survivors-like, "Night Swarm"

**Request:** "remove all the logic related to current game and create a new game" — a survivors-like
(bullet heaven): auto-firing weapons, hordes, XP gems, pick 1 of 3 upgrades, weapon evolutions,
permadeath, gold for permanent unlocks, short runs ending with a boss. Run length chosen: 10 minutes.

**Removed:** every Sun Shrine node (scenes, Kai, monsters, NPCs, maps, music, sounds, dialogs…).
Kept the generic pieces: `font_default`, `pal_ui` (recolored), `icon_cursor`, `icon_heart`,
`sfx_select`, `sfx_confirm`. Engine behaviors stay (they are reusable).

**New game "Night Swarm"** (ROM `dist/night_swarm.gba`, saves to the cartridge):
- Scenes: `scn_title`, `scn_shop` (permanent power ups), `scn_field` (the 10-minute run),
  `scn_game_over`, `scn_victory`.
- Hunter `plr_hunter`: 8-way walking; weapons fire by themselves. Weapons `upg_bolt` (Magic Bolt,
  nearest monster), `upg_knife` (Star Knife, walking direction), `upg_orb` (Holy Orbs, circle you),
  `upg_aura` (Sun Aura). Items `upg_tome`, `upg_wings`, `upg_lodestone`, `upg_heart`, `upg_ring`.
  Evolutions `upg_storm`, `upg_starstorm`, `upg_halo`, `upg_flare` (max weapon + partner item).
  Fillers `upg_gold`, `upg_meat`. Shots `prop_bolt`, `prop_knife`, `prop_orb`, `prop_storm`,
  `prop_blade`, `prop_halo`.
- Monsters `enm_bat`, `enm_zombie`, `enm_skeleton`, `enm_ghost`, `enm_wolf`, boss `enm_lord` (10:00).
  Waves, clock and levels run on the invisible `prop_director`.
- Pickups `item_gem`, `item_gem_big`, `item_coin`, `item_meat`. Menus `menu_title`, `menu_shop`,
  `menu_level_up`, `menu_game_over`, `menu_victory`; HUDs `hud_run`, `hud_title`, `hud_shop`,
  `hud_results`, `hud_victory`; dialog `dlg_how_to_play`.
- Music `mus_title`, `mus_field`, `mus_boss`, `mus_game_over`, `mus_victory`; 11 new sound effects.
- Maps `map_field` (96x96-tile graveyard field), `map_night` (title/menu backdrop).

**Engine and tools (shared):**
- New node type `upgrade` (approved), and menus with `"upgrades": 3` that show random upgrade cards.
- New behaviors: `weapon`, `projectile`, `aura`, `swarm`, `magnet`, `spawn_wave`, `run_clock`,
  `level_up`; `topdown_controller` gained `speed_var`.
- Actions: `if_chance`; `set_var` / `add_var` can copy `from` another variable, `add_var` can cap at
  `max_var`. Variables can be `persistent` (kept by `reset_vars`). HUD/menu text: `{var:02}`, and
  menu labels/titles can show variables. Props may have no body (invisible logic). A logic list may
  use a behavior more than once.
- Performance for hordes (measured in the emulator): actor pool 96 in EWRAM, contact checks by
  category buckets with cached hitboxes, still actors skip physics, hot files moved to IWRAM as ARM
  code (`*.iwram.c`, new build rule), horde aiming every 8 frames, crowd spreading through a small
  occupancy grid, waves spawn at most 3 monsters per frame. Result: ~65 monsters on screen with
  about 1 slow frame per 10 seconds.
- Visualizer: upgrade preview (card per level, recipe), upgrade-menu sample cards, `{var:02}`.

## 2026-10-03 — Replace Hero Quest with a top-down adventure, "Sun Shrine"

**Request:** "remove all the logic related to this game and create a new top down zelda 2d like game".

**Removed:** every Hero Quest node (scenes, hero, slime, coins, lift, sage, flag, tree, maps,
music, sounds…). Kept only generic pieces, now used by the new game: `font_default`, `pal_ui`,
`icon_cursor`, `icon_heart`, `icon_heart_empty`, `sfx_select`, `sfx_confirm`.

**New game "Sun Shrine"** (ROM `dist/sun_shrine.gba`) — 115 new nodes:
- Scenes: `scn_intro` (story), `scn_title`, `scn_meadow` (overworld with village, pond, secret
  nook), `scn_shrine` (dungeon), `scn_game_over`, `scn_victory` (ending).
- Hero `plr_kai`: 8-way walking, faces up/down/left/right, sword on B (after the elder gives it),
  talks with A, 5 hearts.
- Monsters: `enm_blob` (wanders, drops a gem), `enm_bat` (chases), `enm_knight` (3 hits, drops a
  heart). Shared defeat puff `anim_poof`.
- Props and people: `prop_bush` (cut with the sword), `prop_door` (locked, opens with a key),
  `npc_elder` (gives the sword), `npc_merchant` (potion for 10 gems).
- Pickups: `item_gem`, `item_heart`, `item_key`, `item_crystal` (the goal, with `ptc_crystal_glow`).
- World: `ts_meadow`, `ts_shrine`, `map_meadow`, `map_shrine`, `map_title`; trigger `trg_shrine_door`.
- UI: `hud_game`, `hud_intro`, `hud_victory`, `menu_title`, `menu_game_over`, 7 dialogs
  (`dlg_elder`, `dlg_merchant`, `dlg_no_gems`, `dlg_need_sword`, `dlg_key`, `dlg_crystal`,
  `dlg_how_to_play`), icons `icon_gem`, `icon_key`, `icon_elder_face`, `icon_merchant_face`.
- Sound: `mus_title`, `mus_meadow`, `mus_shrine`, `mus_victory`, `mus_game_over` and 11 sound
  effects (sword, hurt, monster hit/defeat, gem, heart, key, door, item fanfare, talk, bush cut).
- Variables: `health`, `gems`, `keys`, `has_sword`, `checkpoint` (TRY AGAIN restarts the area).

**Engine additions (shared, game-agnostic):**
- Actors now face up / down / left / right; bodies can have `idle_up`, `idle_down`, `walk_up`,
  `walk_down`, `attack_up`, `attack_down` animations (falls back to the plain ones).
- New behaviors: `sword_attack`, `wander`, `locked_door`.
- `health`: top-down knockback (straight away from the hit, slowing down), dies in place without
  gravity, new `spikes` option. `topdown_controller` and `chase_player` pick the facing.
  `talk` also works when standing above or below the person.
- Visualizer: knows the new animation slots (up/down art is never mirrored).

## 2026-10-02 — Build the whole system from CLAUDE.md (milestones M0–M6)

**Request:** "implement CLAUDE.md".

**What was made**
- **Tools** (`tools/`): `validate.py` (checks every rule), `codegen.py` (nodes → C data),
  `bundle.py` (visualizer data), `build.py` (validate → codegen → bundle → compile → ROM),
  `run.py` (builds if needed, opens mGBA), shared `common.py`, `config.json`.
- **Buttons for the human**: `build.bat`, `run.bat`, `view.bat`.
- **Catalogs**: 14 behaviors (`catalog/behaviors.json`) and 20 actions (`catalog/actions.json`).
- **Engine** (`src/engine/`): main loop, input, variables, scenes and fades, actors, physics
  (solid / one-way / hazard / ladder tiles, moving platforms), animations, particles, camera,
  scrolling backgrounds with parallax and map streaming, sprites, 4-channel sound (music + sound
  effects), HUD / menus / dialogs, action lists, SRAM save.
- **Behaviors** (`src/behaviors/`): platformer_controller, topdown_controller, patrol,
  chase_player, follow_path, solid_platform, health, damage_on_touch, stompable, collectible,
  trigger_zone, talk, camera_target, spawn_particles.
- **Visualizer** (`visualizer/visualizer.html`): node list with search and filters, tree,
  properties in plain language, and live previews for every node type (scenes, art, maps,
  animations, particles, bodies, actors, HUD, menus, dialogs, sound effects and music with a
  built-in synth).
- **Demo game "Hero Quest"** — 79 nodes:
  - Scenes: `scn_intro`, `scn_title`, `scn_level_1`, `scn_game_over`, `scn_credits`.
  - Actors: `plr_hero`, `enm_slime`, `item_coin`, `plat_lift`, `npc_sage`, `prop_tree`,
    `prop_flag`, `trg_exit` (with their bodies, sprites and animations).
  - World: `map_level_1` + `ts_grass`, `map_hills` + `ts_hills`.
  - UI: `font_default`, `hud_main`, `hud_intro`, `hud_credits`, `menu_title`,
    `menu_game_over`, `dlg_sage`, icons `icon_heart`, `icon_heart_empty`, `icon_coin`,
    `icon_cursor`, `icon_sage_face`.
  - Sound: `mus_title`, `mus_level_1`, `mus_game_over`; `sfx_jump`, `sfx_hurt`, `sfx_stomp`,
    `sfx_coin`, `sfx_select`, `sfx_confirm`, `sfx_talk`.
  - Particles: `ptc_hero_dust`, `ptc_coin_sparkle`. Palettes: `pal_ui`, `pal_hero`,
    `pal_world`, `pal_hills`, `pal_items`, `pal_enemies`, `pal_npc`.

**Docs**: `CLAUDE.md` now records the exact field details, engine rules and VRAM plan;
`SETUP.md` gained how the AI checks a ROM and the visualizer without a human.
