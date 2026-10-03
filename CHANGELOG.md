# Changelog

One entry per request, newest first. Written for humans: what changed and which nodes.

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
