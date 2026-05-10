# Project Layout Guidance

SDL3D games should keep one root game file and split implementation detail into
structured JSON fragments, scene files, Lua scripts, and assets. The goal is
that a developer can infer where to make a change from the path.

## Recommended Data Project Shape

```text
data/
  game.game.json
  fragments/
    actors/
      player.json
      enemies.json
      props.json
      projectiles.json
      effects.json
    world/
      level_1.materials.json
      level_1.rooms.json
      level_1.lights.json
    input/
      actions.json
      device_assignments.json
      profiles.json
    editor/
      metadata.json
    logic/
      gameplay.json
      menus.json
      options.json
      network.json
    network/
      protocol.json
      replication.json
      control_messages.json
      runtime_bindings.json
      session_flow.json
    assets.json
    interaction.json
    persistence.json
    presentation.json
    runtime.json
    scripts.json
  scenes/
    splash.scene.json
    title.scene.json
    play.scene.json
    options.scene.json
  scripts/
    rules.lua
  images/
  audio/
  shaders/
```

This is guidance, not a required exact tree. Use fewer files for small games
and add folders only when they make ownership clearer.

## Root File Responsibilities

The root `sdl3d.game.v0` file is the game entry point. It should contain:

- `schema`
- `imports`
- `metadata`
- `app`
- `scenes`
- `world`

Root-only sections should remain in the root file because they define how the
game starts and how tools discover the game. Large reusable sections such as
actors, grid maps, input, logic, networking, assets, scripts, persistence, and
presentation should usually live in fragments.

## Fragment Responsibilities

Fragments use `sdl3d.fragment.v0` and contribute only mergeable sections. Split
fragments by editing purpose:

- Actor fragments answer "what exists in the world?"
- Archetype/instance actor fragments answer "what repeated placed actors share
  a template?"
- Sector world fragments answer "which materials, sectors, lights, doors, and
  platforms define this level?"
- Grid fragments answer "what tile, maze, or board layout drives this level?"
- Input fragments answer "what can the player do and with which devices?"
- Editor fragments answer "how should tools preview, categorize, place, and
  test reusable authored objects?"
- Logic fragments answer "what happens when a signal, timer, or sensor fires?"
- Network fragments answer "what is replicated and how sessions flow?"
- Presentation fragments answer "how does reusable visual/audio polish behave?"

Prefer paths that match developer intent:

- `fragments/actors/player.json`
- `fragments/grids/level_1_maze.json`
- `fragments/logic/scoring.json`
- `fragments/network/replication.json`
- `fragments/input/profiles.json`

Avoid vague names such as `misc.json`, `stuff.json`, or `gameplay2.json`.

## Split Criteria

Split a file when:

- it contains multiple concepts edited for different reasons
- it is difficult to review because unrelated changes appear together
- it is hard to answer "where do I change this?"
- two developers would naturally edit different parts independently

Do not split a file when:

- the fragment would contain only incidental leftovers
- related behavior would require chasing many tiny files
- the split hides ordering or ownership
- the file is short and already has one clear purpose

As a rough guideline, files over a few hundred lines deserve review, but line
count is not the rule. Clarity of ownership is the rule.

## Import Rules

Imports are structured composition, not textual includes.

- Import paths must be safe relative paths.
- Fragments cannot author root-only sections.
- Arrays append deterministically in import order.
- Objects merge recursively.
- Scalar conflicts fail validation.
- Duplicate authored names are rejected after composition.

Keep the import list in intentional order. If order matters, document it near
the affected data rather than relying on accidental filename sorting.

Composition-time helpers should still preserve clear ownership. Use
`actor_instances` to place repeated static actors from archetypes rather than
duplicating component JSON for every crate, pickup, or lamp. Use
`sector_level_fragments` to split large sector worlds into materials, sectors,
and lights while still producing one validated `sector_levels` entry.

## Templates

Reusable project templates live under `templates/`. They are not magic engine
imports; they are copyable, project-agnostic fragments that show standard
naming and composition patterns for common genres.

Use templates as starting points, then rename ids to match the game. Prefer
templates for reusable primitives such as FPS controls, render profile toggles,
pause/HUD widgets, reticles, and sector interactions. Keep game-specific rules
in the game project's fragments and Lua scripts.

The repository includes a data-only FPS starter at `demos/templates/fps`. It
demonstrates the expected split for a small sector/FPS game:

- `fragments/input/fps_controls.json` for reusable movement, interaction, fire,
  pause, and menu actions.
- `fragments/world/fps_room.json` for a mock sector room plus sector navigation
  graph.
- `fragments/actors/fps_player.json` for a controller-equipped player and
  data-authored projectile weapon.
- `fragments/actors/combat_pickups_projectiles.json` for projectile, pickup,
  and resource-station archetypes, instances, and pools.
- `fragments/ui/fps_hud.json` for reticle, FPS counter, resource text, and
  pause overlay text.
- `fragments/editor/fps_templates.json` for project-agnostic editor palette
  metadata.

The template is intentionally mock-media only. A real game should copy the
shape, rename ids, and replace geometry/colors/assets without introducing a
custom C host.

## Dojos And Editor Metadata

Mechanic dojos are small data-only projects or scenes used to tune reusable
primitives in isolation. Prefer mock geometry, colored lights, particles, and
clear UI indicators over finished media while the mechanic is still evolving.
Launch dojos through `sdl3d_runner` and direct scene selection so mechanics can
be tested without walking through splash, title, or campaign flow.

Use optional `editor` metadata beside the authored object it describes. This
keeps future editor palettes and prefab browsers grounded in the same JSON that
runtime validation sees. Metadata should describe categories, previews, bounds,
snap hints, exposed properties, and recommended test scenes; it should not
encode gameplay rules.

## Lua Placement

Lua belongs under `scripts/` and should hold game-specific rules:

- specialized movement math
- enemy decision policy
- scoring formulas
- procedural spawn choices
- game-specific save-data interpretation

Lua should not duplicate generic data actions. If a rule can be expressed by
JSON signals, sensors, timers, conditions, and actions, keep it in JSON.

## Asset Packaging

Use the same data tree for development directories, `.sdl3dpak` files, and
embedded packs. Build files should include all authored JSON, Lua, shaders, and
media that the game can load through `asset://` paths.

For demo targets with many fragments, prefer a sorted recursive fragment list
in CMake so adding a fragment does not silently break embedded or packed
launch paths.
