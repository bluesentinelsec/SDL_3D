# SDL3D Game Data JSON

SDL3D game data is a JSON-family authoring format for data-driven games. It is
designed so game projects can compose reusable engine primitives in JSON, keep
game-specific rules in Lua, and run through the generic `sdl3d_runner`.

The root schema is currently `sdl3d.game.v0`. Fragment files use
`sdl3d.fragment.v0`.

## Design Goals

- Keep one clear root entry point for each game.
- Keep large games readable through structured imports.
- Validate bad references, duplicate names, unsupported schemas, and invalid
  action payloads before gameplay starts.
- Keep reusable behavior declarative and inspectable.
- Reserve Lua adapters for game-specific calculations and policy.
- Avoid textual includes, macros, or hidden import behavior.

## Root Object

Every root game file is a JSON object.

| Field | Required | Purpose |
| --- | --- | --- |
| `schema` | yes | Root schema id, currently `sdl3d.game.v0`. |
| `imports` | no | Structured references to fragment files. |
| `metadata` | yes | Human-readable game identity and versioning. |
| `app` | no | Managed-loop startup config such as title, logical size, backend, audio, and pause policy. |
| `scenes` | yes | Scene entry point, scene files, and scene packages. |
| `world` | yes | World identity, coordinate policy, cameras, lights, and bounds. |
| `storage` | no | Writable storage identity for `user://` and `cache://`. |
| `persistence` | no | Reusable actor-property persistence entries. |
| `profiles` | no | Runtime/profile hints. |
| `assets` | no | Images, sprites, fonts, audio, shaders, and other asset descriptors. |
| `scripts` | no | Lua script modules. |
| `input` | no | Input contexts, actions, device assignment sets, and profiles. |
| `render` | no | Renderer setup and global render policy. |
| `transitions` | no | Named transition descriptors. |
| `ui` | no | Reusable UI descriptors. |
| `entities` | no | Named actors with tags, transforms, properties, and components. |
| `grid_maps` | no | Authored tile grids for maze, board, and grid-locked games. |
| `grid_pickup_layers` | no | Dense grid-indexed pickup layers rendered and collected without one actor per pickup. |
| `sector_levels` | no | Authored sector/portal worlds for Doom/Quake-style indoor levels. |
| `sector_doors` | no | Runtime sliding doors for sector/FPS worlds. |
| `actor_archetypes` | no | Templates used by runtime actor pools. |
| `actor_pools` | no | Preallocated spawn/despawn pools. |
| `signals` | no | Authored signal names. |
| `logic` | no | Sensors, timers, signal bindings, conditions, and actions. |
| `adapters` | no | Lua or native extension points for game-specific behavior. |
| `network` | no | Optional networking schema and session-flow metadata. |
| `haptics` | no | Haptics policy. |
| `presentation` | no | Shared presentation policy and effects. |
| `update_phases` | no | Authored update-phase policy. |

Root files should normally own `schema`, `imports`, `metadata`, `app`,
`scenes`, and `world`. Large reusable sections should usually move to
fragments.

## Naming

Names are stable authored handles. Use namespaces that reveal ownership:

- `entity.player`
- `entity.enemy.basic`
- `camera.main`
- `signal.score.add`
- `logic.goal.player`
- `timer.round.start`
- `adapter.rules.reflect_collision`

Loaders reject duplicate names inside namespaces and report the source file and
JSON path when validation fails.

## Structured Imports

Imports are structured composition, not textual includes. Each imported file is
a JSON object with schema `sdl3d.fragment.v0`.

```json
{
  "schema": "sdl3d.game.v0",
  "imports": [
    { "path": "fragments/assets.json", "sections": ["assets"] },
    { "path": "fragments/actors/player.json", "sections": ["entities"] },
    { "path": "fragments/world/e1m1.sectors.json", "sections": ["sector_levels"] },
    { "path": "fragments/world/e1m1.doors.json", "sections": ["sector_doors", "signals", "logic"] },
    { "path": "fragments/input/profiles.json", "sections": ["input"] },
    { "path": "fragments/network/replication.json", "sections": ["network"] }
  ]
}
```

Fragment example:

```json
{
  "schema": "sdl3d.fragment.v0",
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "tags": ["player"],
      "transform": { "position": [0.0, 0.0, 0.0] }
    }
  ]
}
```

Import rules:

- paths are relative to the importing file
- absolute paths, URI schemes, backslashes, drive prefixes, `.`, `..`, and
  empty path segments are rejected
- fragments may import other fragments
- cycles and excessive depth are rejected
- root-only sections such as `metadata`, `app`, `world`, and scene entry-point
  ownership are not allowed in fragments
- `sections` must name mergeable sections, and selected sections must exist
- non-selected mergeable sections in the fragment are rejected
- arrays concatenate in authored import order
- objects merge recursively
- scalar/type conflicts fail validation

See [Project Layout Guidance](project-layout.md) for recommended fragment
organization.

## Project Layout

Recommended game projects keep edit surfaces obvious:

```text
data/
  game.game.json
  fragments/
    actors/
    input/
    logic/
    network/
    assets.json
    presentation.json
  scenes/
  scripts/
  images/
  audio/
```

Split by developer intent, not by schema mechanics alone. A file named
`fragments/logic/scoring.json` is better than a generic `logic2.json`.

## App

The optional `app` object configures the managed loop before the window exists:

```json
{
  "app": {
    "title": "Example Game",
    "logical_width": 1280,
    "logical_height": 720,
    "backend": "opengl",
    "tick_rate": 60,
    "audio": { "enabled": true },
    "pause": { "action": "action.pause" }
  }
}
```

## World Cameras

`world.cameras` defines reusable render cameras by name. Scenes can select a
camera with their `camera` field, and logic actions can switch cameras with
`camera.set` or `camera.toggle`.

Camera types:

- `orthographic`: fixed position/target/up with `size`.
- `perspective`: fixed position/target/up with `fovy`.
- `chase`: follows an actor named by `target_entity`. The forward vector comes
  from a Vec3 actor property such as `velocity` or an authored
  `camera_forward`, with `fallback_forward` used when that property is near
  zero. Use `chase_distance: 0` for actor-perspective cameras and larger
  values for behind-the-actor chase cameras.
- `fps`: reads a first-person sector controller from `target_entity` and
  renders from that actor's eye position. If the controller has not updated
  yet, the camera falls back to the actor's `yaw`, `pitch`, and `view_smooth`
  properties.
- `adapter`: delegates camera ownership to a native or Lua adapter.

## Storage And Persistence

`storage` declares platform-stable writable roots:

```json
{
  "storage": {
    "organization": "Example Studio",
    "application": "Example Game",
    "profile": "default"
  }
}
```

Use `user://` for player-owned data and `cache://` for disposable generated
data. See [Storage](storage.md).

`persistence` entries save and load allowlisted actor properties:

```json
{
  "persistence": {
    "entries": [
      {
        "name": "persistence.options",
        "path": "user://settings/options.json",
        "schema": "example.options.v1",
        "version": 1,
        "target": "entity.settings",
        "properties": ["display_mode", "vsync", "sfx_volume"]
      }
    ]
  }
}
```

## World

`world` declares the game's spatial context. It does not imply a sector map or
any other specific world type.

```json
{
  "world": {
    "name": "world.main",
    "kind": "fixed_screen",
    "units": "world_units",
    "axes": { "horizontal": "x", "vertical": "y", "depth": "z" },
    "bounds": { "min": [-9.0, -5.0, -1.0], "max": [9.0, 5.0, 2.0] },
    "cameras": [],
    "lights": []
  }
}
```

World kinds may include fixed-screen playfields, tile grids, room graphs,
sector/portal maps, brush worlds, or general 3D scenes as engine support
grows.

## Sector Levels

`sector_levels` describe sector/portal worlds as data. This is the reusable
foundation for Doom-like indoor maps: materials, sector floor plans, heights,
sector metadata, and baked lights are authored in JSON and loaded into runtime
`sdl3d_level` variants.

```json
{
  "sector_levels": [
    {
      "name": "sector.e1m1",
      "materials": [
        {
          "name": "rock_floor",
          "texture": "asset://textures/rock_floor.jpg",
          "albedo": [1.0, 1.0, 1.0, 1.0],
          "roughness": 0.9,
          "tex_scale": 4.0
        },
        {
          "name": "wall_metal",
          "texture": "asset://textures/wall_metal.jpg",
          "roughness": 0.7,
          "tex_scale": 4.0
        }
      ],
      "sectors": [
        {
          "name": "start_room",
          "points": [[0, 0], [10, 0], [10, 8], [0, 8]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "rock_floor",
          "ceil_material": "wall_metal",
          "wall_material": "wall_metal"
        },
        {
          "name": "hazard_basin",
          "points": [[-2, 16], [10, 16], [10, 26], [-2, 26]],
          "floor_y": -0.5,
          "ceil_y": 4.5,
          "floor_material": "rock_floor",
          "ceil_material": "wall_metal",
          "wall_material": "wall_metal",
          "damage_per_second": 18.0,
          "ambient_sound_id": 1,
          "push_velocity": [0.0, 0.0, 0.0]
        }
      ],
      "lights": [
        {
          "position": [5.0, 3.5, 4.0],
          "color": [1.0, 0.82, 0.58],
          "intensity": 2.4,
          "range": 9.5
        }
      ]
    }
  ]
}
```

Sector material references may be material names or zero-based material
indices. Prefer names for maintainability. `floor_material` and
`ceil_material` may be omitted, `null`, `-1`, or `"none"` to omit that
surface; `wall_material` must reference a declared material.

Validation requires:

- unique sector level names
- a non-empty `materials` array with unique material names
- a non-empty `sectors` array
- each sector to contain 3..32 vec2 `points`
- numeric `floor_y` and `ceil_y`, with `ceil_y > floor_y`
- valid material references
- optional `floor_normal`, `ceil_normal`, and `push_velocity` as vec3 arrays
- non-negative `ambient_sound_id` and `damage_per_second`
- optional lights with `position` vec3, optional `color` vec3, non-negative
  `intensity`, and positive `range`

At load time SDL3D builds three runtime variants per sector level:

- `lightmapped`: baked lights plus lightmap atlas data
- `vertex_baked`: baked vertex lighting without a lightmap atlas
- `unlit`: raw material color/texture without baked lights

Scenes render sector levels by declaring instances under `world.sector_levels`:

```json
{
  "schema": "sdl3d.scene.v0",
  "name": "scene.level_1",
  "camera": "camera.level_1",
  "world": {
    "sector_levels": [
      {
        "level": "sector.e1m1",
        "variant": "lightmapped",
        "position": [0.0, 0.0, 0.0],
        "portal_culling": true
      }
    ]
  }
}
```

`level` references a top-level sector level. `variant` defaults to
`lightmapped` and may be `lightmapped`, `vertex_baked`, or `unlit`.
`position` defaults to the origin and translates the level as a whole.
`portal_culling` defaults to `true`; when enabled, generic presentation
computes visibility from the active camera before drawing the level.

Renderer, controller, and editor phases should consume runtime descriptors
instead of parsing JSON directly. `world.kind` remains a high-level statement
of spatial intent; `sector_levels` is the concrete sector-world data.

`sector_doors` add dynamic, collidable door panels to sector/FPS scenes without
hard-coding them in a demo host. A door has one or two axis-aligned panels,
closed bounds, an open offset, animation timing, optional scene scoping, and
optional cube-render settings:

```json
{
  "sector_doors": [
    {
      "name": "door.security.east",
      "id": 12,
      "scene": "scene.level_1",
      "open_seconds": 0.7,
      "close_seconds": 0.7,
      "stay_open_seconds": 5.0,
      "panels": [
        {
          "bounds": { "min": [9.85, -0.5, 18.0], "max": [10.15, 3.5, 22.0] },
          "open_offset": [0.0, 4.2, 0.0]
        }
      ],
      "render": { "color": [145, 165, 190, 255], "lighting": true }
    }
  ]
}
```

Doors update each frame while their scene is active, emit render primitives
through the normal authored primitive path, and participate in
`controller.fps_sector` collision as dynamic obstacles. `scene` may be omitted
for a door that is active in every scene. `render.texture` may reference an
image asset id.

Door actions are data-authored:

```json
{ "type": "sector_door.open", "target": "door.security.east", "stay_open_seconds": 5.0 }
{ "type": "sector_door.close", "target": "door.security.east" }
{ "type": "sector_door.toggle", "target": "door.security.east" }
```

Use `target_from_payload` when a previous action picked the door. The
`sector_door.interact` action finds the nearest active door in front of an
actor and either emits a signal or runs nested actions with `actor_name`,
`door_name`, and `door_id` payload fields:

```json
{
  "type": "sector_door.interact",
  "actor": "entity.player",
  "range": 2.25,
  "min_dot": 0.15,
  "actions": [
    { "type": "sector_door.open", "target_from_payload": "door_name" }
  ]
}
```

If no door is in range and in front of the actor, the interaction is treated as
a successful no-op so use-button bindings can be authored without extra guard
conditions.

Use `controller.fps_sector` on an actor to drive first-person movement through
a sector level:

```json
{
  "name": "entity.player",
  "active": true,
  "transform": { "position": [5.0, 1.6, 4.0] },
  "properties": {
    "yaw": { "type": "float", "value": 3.14159 },
    "pitch": { "type": "float", "value": 0.0 },
    "current_sector": { "type": "int", "value": -1 }
  },
  "components": [
    {
      "type": "controller.fps_sector",
      "sector_level": "sector.e1m1",
      "actions": {
        "forward": "action.move.forward",
        "back": "action.move.back",
        "left": "action.move.left",
        "right": "action.move.right",
        "jump": "action.jump"
      },
      "move_speed": 12.0,
      "jump_velocity": 6.0,
      "gravity": 14.0,
      "player_height": 1.6,
      "player_radius": 0.35,
      "step_height": 1.1,
      "ceiling_clearance": 0.1,
      "mouse_sensitivity": 0.002
    }
  ]
}
```

The actor transform is the player's eye position; feet are
`position.y - player_height`. The controller applies sector collision, stair
stepping, wall sliding, gravity, jumping, and mouse-look. It writes the actor's
position plus diagnostic properties each frame. Property names can be customized
with `yaw_property`, `pitch_property`, `view_smooth_property`,
`vertical_velocity_property`, `on_ground_property`, and `sector_property`; the
defaults are `yaw`, `pitch`, `view_smooth`, `vertical_velocity`, `on_ground`,
and `current_sector`. Use an `fps` camera targeting the same actor for the
player view.

## Scenes

`scenes` defines the initial scene and scene files or packages:

```json
{
  "scenes": {
    "initial": "scene.title",
    "files": [
      "scenes/title.scene.json",
      { "package": "standard_options" },
      "scenes/play.scene.json"
    ]
  }
}
```

Scene files use `sdl3d.scene.v0`. They can select entities, input actions,
menus, UI text, update phases, transitions, and activity hooks.

`scenes.initial` is the normal startup scene. Development tools may override it
at launch time; for example, `sdl3d_runner --scene scene.level_1` enters a
loaded scene directly and may inject scene state before that scene's enter
signal runs. Authored data should still define a valid `scenes.initial` so the
game has a complete production startup path.

## Actors

Actors describe runtime objects:

```json
{
  "name": "entity.player",
  "active": true,
  "tags": ["player"],
  "transform": { "position": [0.0, 0.0, 0.0] },
  "properties": {
    "speed": { "type": "float", "value": 7.5 }
  },
  "components": [
    { "type": "render.sprite", "sprite": "sprite.player" },
    { "type": "collision.aabb", "half_extents": [0.4, 0.4, 0.1] }
  ]
}
```

Properties support scalar and vector values used by logic, UI, persistence,
network replication, and Lua adapters.

## Actor Archetypes And Pools

Runtime spawning uses preallocated pools:

```json
{
  "actor_archetypes": [
    {
      "name": "archetype.enemy.basic",
      "tags": ["enemy"],
      "components": [{ "type": "render.sprite", "sprite": "sprite.enemy" }]
    }
  ],
  "actor_pools": [
    {
      "name": "pool.enemies",
      "archetype": "archetype.enemy.basic",
      "capacity": 64,
      "on_exhausted": "fail",
      "on_scene_exit": "reset"
    }
  ]
}
```

Pools support deterministic spawn/despawn, scene-exit policy, lifecycle safety,
metrics, and Lua helpers.

Common projectile archetype conventions:

- tag projectile actors with a broad tag such as `projectile` and a role tag
  such as `player_projectile` or `enemy_projectile`
- store `velocity` as a `vec3` property and add a
  `{ "type": "motion.velocity_2d", "property": "velocity" }` component
- store collision shape data with either `radius` or `half_width` /
  `half_height`
- store gameplay fields such as `damage`, `owner`, and `ttl` as authored
  properties so JSON actions and Lua can read the same data
- add optional `light.point` and `particles.emitter` components on projectile
  or explosion archetypes when pooled effects should emit light or particles

Example:

```json
{
  "name": "archetype.player_shot",
  "tags": ["projectile", "player_projectile"],
  "properties": {
    "radius": { "type": "float", "value": 0.12 },
    "damage": { "type": "int", "value": 1 },
    "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
  },
  "components": [
    { "type": "motion.velocity_2d", "property": "velocity" },
    { "type": "light.point", "color": [0.2, 0.8, 1.0], "intensity": 2.0, "range": 1.8 }
  ]
}
```

`projectile.fire` is a reusable action for firing from an actor into a pool:

```json
{
  "type": "projectile.fire",
  "target": "entity.player",
  "pool": "pool.player_shots",
  "offset": [0.6, 0.0, 0.1],
  "velocity": [12.0, 0.0, 0.0],
  "cooldown_property": "fire_timer",
  "properties": { "owner": 1, "damage": 1 }
}
```

The target actor may define `fire_cooldown`; otherwise the action's `cooldown`
field is used. If the cooldown property is positive, no projectile is spawned.
Use `target_from_payload` instead of `target` when the firing actor should come
from a signal or collision payload.

`actor.spawn` and `actor.despawn` can also resolve actors from payload fields:

```json
{ "type": "actor.spawn", "pool": "pool.explosions", "from_payload": "other_actor_name" }
{ "type": "actor.despawn", "target_from_payload": "actor_name" }
```

This keeps common collision effects data-authored while still allowing Lua to
provide game-specific policy when needed.

## Grid Maps And Maze Primitives

`grid_maps` describe deterministic tile grids for maze, board, and
grid-locked arcade games. They are game-agnostic data: the grid tells the
engine which cells exist, which glyphs are walkable, how cells map to world
space, and whether movement wraps at the edges.

```json
{
  "grid_maps": [
    {
      "name": "map.maze",
      "origin": [0.0, 0.0, 0.0],
      "cell_size": [1.0, 1.0],
      "row_direction": -1.0,
      "wrap_x": true,
      "walkable": [" ", ".", "o", "P", "G"],
      "rows": [
        "#####",
        "#P.o#",
        "# # #",
        "#G..#",
        "#####"
      ]
    }
  ]
}
```

Rows must be non-empty strings with identical widths. Glyphs are single-byte
characters; use Lua or imported fragments for higher-level meaning such as
`P = player spawn`, `G = ghost spawn`, `.` = pellet, or `o` = power pellet.
Cell `(0, 0)` maps to `origin`; increasing columns move along +x; increasing
rows move along `cell_size.y * row_direction`.

`motion.grid_agent` moves an actor from cell center to cell center with queued
turns and wall blocking:

```json
{
  "name": "entity.player",
  "properties": {
    "grid_col": { "type": "int", "value": 1 },
    "grid_row": { "type": "int", "value": 1 },
    "grid_dir_x": { "type": "int", "value": 1 },
    "grid_dir_y": { "type": "int", "value": 0 },
    "grid_next_dir_x": { "type": "int", "value": 0 },
    "grid_next_dir_y": { "type": "int", "value": 0 },
    "grid_speed": { "type": "float", "value": 6.0 }
  },
  "components": [
    { "type": "motion.grid_agent", "map": "map.maze", "speed": 6.0 }
  ]
}
```

`grid_speed` is measured in cells per second. Scripts or input actions can set
`grid_next_dir_x` and `grid_next_dir_y`; the component applies the queued turn
at the next cell center if the target cell is walkable, otherwise it continues
in the current direction when possible.

`grid_pickup_layers` represent dense collectible maps as a runtime bitset
instead of one actor per pickup. Each layer references a `grid_maps` entry and
declares the glyphs that should be collectible and batched for rendering:

```json
{
  "grid_pickup_layers": [
    {
      "name": "pickup.collectibles",
      "map": "map.maze",
      "kinds": [
        { "glyph": ".", "kind": "pellet", "points": 10, "radius": 0.07 },
        { "glyph": "o", "kind": "power", "points": 50, "radius": 0.14 }
      ]
    }
  ]
}
```

Reset a layer from its map glyphs with `grid.pickup_layer.reset`; Lua can then
use `ctx:grid_pickup_at(...)`, `ctx:grid_collect_at(...)`, and
`ctx:grid_pickup_count(...)` for O(1) pickup checks without scanning actors.
Pickup layers are intended for dense pellets, coins, keys, and similar static
collectibles. Use actor pools when each pickup needs independent behavior,
collision shape state, or per-actor scriptable lifecycle.
Pickup layer rendering emits a batched sphere primitive so a dense grid costs
one draw submission per pickup kind instead of one actor and draw submission per
cell.

`grid.spawn_from_glyphs` populates actor pools from map glyphs. This is useful
for pickups that need actor identity, destructible tiles, and spawn markers.
Spawned actors receive `grid_map`, `grid_col`, `grid_row`, and `grid_glyph`
properties in addition to their archetype defaults and authored overrides.
Use pool scene-exit policies or `actor.despawn_by_tag` to reset grids between
levels.

`grid.spawn_runs_from_glyphs` batches contiguous glyph runs into one actor per
run. This is intended for solid maze walls, platforms, and other rectangular
tile runs where one long actor is cheaper than one actor per tile:

```json
{
  "type": "grid.spawn_runs_from_glyphs",
  "map": "map.maze",
  "axis": "x",
  "depth": 0.25,
  "inset": 0.03,
  "output_count_key": "spawned_wall_runs",
  "spawns": [
    { "glyph": "#", "pool": "pool.walls" }
  ]
}
```

Run actors receive the same `grid_*` properties as single-cell spawns, plus
`grid_run_axis`, `grid_run_start_col`, `grid_run_start_row`,
`grid_run_end_col`, `grid_run_end_row`, `grid_run_length`, and
`grid_run_size`. A `render.cube` can consume that size using
`"size_property": "grid_run_size"`.

## Components

Reusable components include:

- `motion.velocity_2d`: moves an actor by a `vec3` velocity property on the x/y
  plane.
- `motion.velocity_3d`: moves an actor by a `vec3` velocity property on all
  axes. This is useful for effect actors, projectiles, particles represented as
  pooled actors, and other simple kinematic objects.
- `lifecycle.ttl`: increments an age property every update and despawns pooled
  actors once the configured TTL is reached. Use this for short-lived bursts and
  effects instead of scanning active actors in Lua.
- `motion.grid_agent`: moves an actor along an authored `grid_maps` maze with
  queued turns, walkability checks, center snapping, and optional map wrapping.
- `controller.fps_sector`: first-person movement through a sector level using
  authored input actions, sector collision, jumping, gravity, stair stepping,
  and mouse-look.
- `motion.scroll_wrap`: scrolls an actor along one axis and wraps to the
  opposite bound. This is intended for parallax panels, repeating stars, clouds,
  conveyor belts, and similar backgrounds.
- `motion.oscillate` and `motion.spin`: simple authored movement effects.
- `light.point`, `light.spot`, and `light.directional`: actor-attached light
  components. They work on static actors and active pooled actors. Component
  lights inherit the actor transform and may use an `offset`.
- `particles.emitter`: actor-attached particle emitter. On pooled actors, the
  emitter is active only while the actor is active.
- `render.cube`: renders a cube using authored `size`, or a vec3 actor property
  named by `size_property`. The property path is useful for grid wall runs and
  other pooled actors that need per-instance dimensions.

Example parallax strip:

```json
{
  "name": "entity.background.nebula_strip",
  "active": true,
  "transform": { "position": [4.0, 2.0, -0.2] },
  "components": [
    { "type": "render.cube", "size": [4.0, 0.1, 0.1], "color": [50, 80, 180, 180] },
    { "type": "motion.scroll_wrap", "axis": "x", "speed": -0.7, "min": -9.0, "max": 9.0 }
  ]
}
```

## Input

Input data declares actions and how devices bind to those actions:

```json
{
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          {
            "name": "action.move.left",
            "bindings": [
              { "device": "keyboard", "key": "LEFT" },
              { "device": "gamepad", "button": "dpad_left" }
            ]
          }
        ]
      }
    ],
    "device_assignment_sets": [],
    "profiles": []
  }
}
```

Profiles can apply different bindings based on scene state and connected
gamepad count.

## Logic

Logic connects sensors, timers, signals, conditions, and actions:

```json
{
  "signals": ["signal.enemy.hit"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.enemy.hit",
        "actions": [
          { "type": "property.add", "target": "entity.score", "key": "value", "value": 10 }
        ]
      }
    ]
  }
}
```

Use JSON actions for ordinary composition. Use Lua adapters only for
game-specific calculations or policy that is not a reusable engine primitive.

`property.set` and `property.add` normally target a fixed actor:

```json
{ "type": "property.add", "target": "entity.player", "key": "health", "value": -1 }
```

Use `target_from_payload` when a sensor or previous action supplies the actor
name. Use `value_from_payload` when the amount should come from sensor payload
data. When `property.add` combines an integer with a float, the result is stored
as a float so fractional damage, timers, and meters can accumulate correctly:

```json
{
  "type": "property.add",
  "target_from_payload": "actor_name",
  "key": "damage_taken",
  "value_from_payload": "sector_damage_delta"
}
```

`collision.on_overlap` is a data-action variant of the 2D contact sensor. It
matches actors by fixed actor names or tags, publishes `actor_name` and
`other_actor_name` in the action payload, and runs actions when overlap occurs.

```json
{
  "name": "sensor.projectile_hits_enemy",
  "type": "collision.on_overlap",
  "a_tag": "player_projectile",
  "b_tag": "enemy",
  "edge": "enter",
  "actions": [
    { "type": "actor.despawn", "target_from_payload": "actor_name" },
    { "type": "actor.spawn", "pool": "pool.explosions", "from_payload": "other_actor_name" },
    { "type": "actor.despawn", "target_from_payload": "other_actor_name" },
    { "type": "property.add", "target": "entity.game", "key": "score", "value": 100 }
  ]
}
```

`edge` may be `enter` for first contact only or `stay` / `overlap` for every
update while actors overlap.

`sensor.sector` detects when an actor is inside an authored sector from a
`sector_levels` entry. It can fire on `enter`, `stay` / `overlap`, or `exit`;
authors can use separate sensors for different edges. The payload includes
`actor_name`, `sector_level`, `sector_name`, `sector_index`,
`sector_damage_per_second`, `sector_damage_delta`, and `ambient_sound_id`.
`sector_damage_delta` is `damage_per_second * dt`, so damage-over-time sectors
can be authored without Lua:

```json
{
  "name": "sensor.nukage.damage",
  "type": "sensor.sector",
  "actor": "entity.player",
  "sector_level": "sector.doom.e1m1",
  "sector": "nukage_basin",
  "edge": "stay",
  "actions": [
    {
      "type": "property.add",
      "target_from_payload": "actor_name",
      "key": "damage_taken",
      "value_from_payload": "sector_damage_delta"
    }
  ]
}
```

Use `actor_tag` instead of `actor` to apply a sector sensor to every active
actor with a matching tag. `sector_index` may be used instead of `sector` when
the authored sector order is deliberate and stable.

`logic.wave_schedules` spawn actors from pools over time:

```json
{
  "logic": {
    "wave_schedules": [
      {
        "name": "wave.basic_enemies",
        "pool": "pool.enemies",
        "interval": 0.75,
        "initial_delay": 0.25,
        "max_active_tag": "enemy",
        "max_active": 24,
        "active_if": { "type": "property.compare", "target": "entity.game", "key": "game_over", "op": "==", "value": false },
        "position": { "x": 9.0, "y": [-3.5, 3.5], "z": 0.2 },
        "velocity": [-3.0, 0.0, 0.0],
        "properties": { "hp": 1, "points": 100 }
      }
    ]
  }
}
```

Schedules are deterministic in structure but can use random ranges for axis
values. `catch_up: true` allows multiple spawns after a long frame; by default a
schedule spawns at most once per update.

## Scene Templates

Reusable scene fragments should be treated as conventions rather than hidden
engine behavior. A professional data-only project should keep title, play,
pause, and game-over HUD files in obvious places, for example:

```text
data/
  scenes/
    splash.scene.json
    title.scene.json
    play.scene.json
    pause.scene.json        # optional if pause UI is separate
    game-over.scene.json    # optional if game-over UI is separate
  fragments/
    actors/
    logic/
    ui/
      hud.json
      menus.json
```

For compact arcade games it is also valid to keep pause and game-over menus in
the play scene using `active_if` conditions. The important rule is that reusable
presentation structure stays in JSON, while game-exclusive rules stay in Lua.

## Lua Scripts And Adapters

Lua-backed adapters are declared through `scripts` and `adapters`:

```json
{
  "scripts": [
    {
      "id": "script.rules",
      "path": "scripts/rules.lua",
      "module": "game.rules",
      "autoload": true
    }
  ],
  "adapters": [
    {
      "name": "adapter.rules.reflect_collision",
      "kind": "action",
      "script": "script.rules",
      "function": "reflect_collision"
    }
  ]
}
```

See [Gameplay Lua API](game-data-lua.md).

Good adapter use:

- choose a constrained random spawn vector
- calculate game-specific enemy steering
- apply special collision response math
- interpret game-specific save payloads

Poor adapter use:

- increment a property
- start a timer
- emit a signal
- change scenes
- reset a transform

Those are generic actions and should stay in JSON.

## Network

Local-only games can omit `network`. Network-enabled games can author:

- protocol id/version/transport/tick rate
- replication channels
- control messages
- runtime binding semantics
- scene-state key mappings
- session-flow scenes and events
- managed runtime policy
- diagnostics

The managed runtime expects semantic binding names such as `state_snapshot`,
`client_input`, `start_game`, `pause_request`, `resume_request`, and
`disconnect`, but games can map those semantics to their own concrete channel
and control names.

Networking is optional. Omitting the network block does not force a local-only
game to link game logic to network behavior.

## Validation

Game data is validated before runtime state is instantiated. Hosts and tools
can call `sdl3d_game_data_validate_file()` directly. Runtime loading runs the
same validation pass before actors, input, scripts, timers, and signal handlers
are wired.

Validation reports authored-content errors with a source file and best-effort
JSON path. It checks:

- schema support
- duplicate authored names
- unsafe import paths, cycles, and invalid fragment sections
- missing entity, signal, adapter, script, camera, timer, action, and network
  references
- script ids, modules, dependencies, dependency cycles, and file existence
- input binding structure
- component, sensor, timer, action, app, UI, render, and camera payloads
- network protocol, replication fields, control messages, runtime bindings,
  directions, schema hash shape, and managed session-flow metadata

Validation warnings are non-fatal by default. Tools can set
`treat_warnings_as_errors` in `sdl3d_game_data_validation_options` for stricter
authoring.
