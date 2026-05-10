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
| `editor` | no | Optional authoring metadata for tools, templates, previews, and dojos. Ignored by runtime gameplay. |
| `entities` | no | Named actors with tags, transforms, properties, and components. |
| `grid_maps` | no | Authored tile grids for maze, board, and grid-locked games. |
| `grid_pickup_layers` | no | Dense grid-indexed pickup layers rendered and collected without one actor per pickup. |
| `sector_levels` | no | Authored sector/portal worlds for Doom/Quake-style indoor levels. |
| `sector_level_fragments` | no | Composition-only material/sector/light fragments merged into named sector levels before validation. |
| `sector_navigation` | no | Authored sector navigation graphs for AI/path queries in sector worlds. |
| `sector_doors` | no | Runtime sliding doors for sector/FPS worlds. |
| `sector_platforms` | no | Runtime moving floor/ceiling sectors for lifts, elevators, and oscillating platforms. |
| `actor_archetypes` | no | Templates used by placed actor instances and runtime actor pools. |
| `actor_instances` | no | Composition-time placed actors generated from archetypes. |
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

## Render

The root `render` object configures frame-level presentation defaults:

```json
{
  "render": {
    "clear_color": [8, 10, 14, 255],
    "lighting": true,
    "lighting_key": "render_lighting_enabled",
    "bloom": true,
    "ssao": true,
    "profile": "modern",
    "profile_key": "render_profile"
  }
}
```

`profile` may be `modern`, `ps1`, `n64`, `dos`, `snes`, `grayscale`, or
`gameboy`. Each profile selects both reusable render settings and a named display
treatment; capable backends apply profile-specific post-processing so retro
profiles visibly change the final image instead of only changing internal
shading flags. Retro profiles use an authored virtual presentation resolution
and upscale treatment: for example, `ps1` presents the scene as a low-resolution
nearest-upscaled image with dithering and color reduction, `n64` uses a softer
low-resolution treatment, `grayscale` uses an old Macintosh-like monochrome
treatment, and `gameboy` uses a low-resolution green-scale palette. UI overlays
are drawn after this presentation pass so menus and HUD text remain readable. If
`tonemap` is not authored, the selected profile supplies its tonemap mode.
`tonemap` may be `none`, `reinhard`, or `aces` when a game wants to override the
profile. Optional `*_key` fields read scene-state values at draw time and
override the authored defaults. This lets games author debug/profile toggles
through sensors and logic actions while keeping the runner game-agnostic.

## Structured Imports

Imports are structured composition, not textual includes. Each imported file is
a JSON object with schema `sdl3d.fragment.v0`.

```json
{
  "schema": "sdl3d.game.v0",
  "imports": [
    { "path": "fragments/assets.json", "sections": ["assets"] },
    { "path": "fragments/actors/player.json", "sections": ["entities"] },
    { "path": "fragments/world/e1m1.materials.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/world/e1m1.sectors.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/world/e1m1.doors.json", "sections": ["assets", "sector_doors", "signals", "logic"] },
    { "path": "fragments/world/e1m1.platforms.json", "sections": ["sector_platforms"] },
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
- `actor_instances` and `sector_level_fragments` are composition-time authoring
  helpers; they are expanded into normal `entities` and `sector_levels` before
  ordinary validation and runtime loading
- `factions` is mergeable so combat relationship policy can live in shared
  gameplay fragments
- `editor` metadata is mergeable so templates and future editor palettes can
  live beside the fragments they describe

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

## Assets

The optional `assets` object gives reusable ids to media. Logic and UI should
refer to these ids instead of hard-coded paths where possible.

Audio assets are split by playback role:

```json
{
  "assets": {
    "sounds": [
      { "id": "sound.ui.select", "path": "asset://audio/select.wav", "volume": 0.7, "bus": "sfx" }
    ],
    "music": [
      { "id": "music.title", "path": "asset://audio/title.ogg", "volume": 0.5, "loop": true }
    ],
    "ambient": [
      {
        "id": "ambient.zone.upper_deck",
        "ambient_id": 1,
        "path": "asset://audio/upper-deck-loop.ogg",
        "volume": 0.45,
        "loop": true
      }
    ]
  }
}
```

`assets.ambient` entries map non-negative `ambient_id` values, including ids
emitted by sector sensors, to loopable environmental streams. `ambient_id: 0`
is reserved by convention for silence and does not need an asset entry.

3D model assets are declared in `assets.models`:

```json
{
  "assets": {
    "models": [
      { "id": "model.dragon", "path": "asset://models/dragon/scene.gltf" }
    ]
  }
}
```

Model assets are rendered with `render.model`. Current model loading uses the
engine's filesystem-backed model loaders, so model paths must resolve from a
directory-mounted asset root. Source-directory development builds support GLTF,
GLB, OBJ, and FBX; pack/embedded model loading should prefer self-contained GLB
assets until model loaders can read every dependency directly through the asset
resolver.

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
    "startup_transition": "startup",
    "pause": { "action": "action.pause" },
    "quit": { "action": "action.exit", "transition": "quit" },
    "input_policy": {
      "global_actions": ["action.exit"]
    }
  }
}
```

The standard authored virtual resolution is 1280x720. The managed window path
presents the logical resolution with letterboxing, so the game scales up or down
to the user's display while preserving the authored 16:9 aspect ratio. Prefer
root-level `logical_width` and `logical_height` for this virtual canvas. The
optional `app.window.width` and `app.window.height` fields describe an initial
desktop window size; they do not change the authored layout scale unless
`app.window.logical_width` and `app.window.logical_height` are also set.

`pause.action` toggles the managed runtime pause state when the active scene
allows the action and `pause.allowed_if` is absent or true. `quit.action`
requests shutdown through the managed app flow; when `quit.transition` names an
authored transition, shutdown is delayed until that transition finishes. Use
`app.input_policy.global_actions` for lifecycle actions such as quit that should
work even when a scene restricts its local `input.actions` list.

## UI Rectangles

`ui.rects` draws solid overlay rectangles in the generic presentation path.
Use them for reusable presentation feedback such as low-health tints, damage
vignettes, shield flashes, letterbox bars, or panel backgrounds without adding
host-specific drawing code.

```json
{
  "ui": {
    "rects": [
      {
        "name": "ui.damage.top",
        "x": 0.0,
        "y": 0.0,
        "w": 1.0,
        "h": 0.08,
        "normalized": true,
        "color": [220, 20, 20, 170],
        "alpha_source": {
          "target": "entity.player",
          "key": "last_damage_per_second",
          "scale": 0.033333,
          "min": 0.35,
          "max": 1.0
        },
        "visible_if": {
          "type": "property.compare",
          "target": "entity.player",
          "key": "last_damage_per_second",
          "op": ">",
          "value": 0.0
        },
        "pulse_alpha": true,
        "pulse_rate": 2.8,
        "pulse_min": 0.45,
        "pulse_max": 1.0
      }
    ]
  }
}
```

`alpha_source` accepts integer or float properties and multiplies the authored
color alpha by `clamp(value * scale, min, max)`. Pair it with `visible_if` when
zero-valued feedback should disappear entirely.

## World Cameras

`world.cameras` defines reusable render cameras by name. Scenes can select a
camera with their `camera` field, and logic actions can switch cameras with
`camera.set` or `camera.toggle`.

Camera types:

- `orthographic`: fixed position/target/up with `size`.
- `perspective`: fixed position/target/up with `fov` and optional `fov_axis`.
  `fov_axis` may be `vertical` or `horizontal`; omitted cameras use vertical
  FOV. The legacy `fovy` field is still accepted as a vertical-FOV alias when
  `fov` is absent. If omitted, the vertical field of view defaults to 90
  degrees.
- `chase`: follows an actor named by `target_entity`. The forward vector comes
  from a Vec3 actor property such as `velocity` or an authored
  `camera_forward`, with `fallback_forward` used when that property is near
  zero. Use `chase_distance: 0` for actor-perspective cameras and larger
  values for behind-the-actor chase cameras. If omitted, FOV defaults to 90
  degrees on the vertical axis.
- `fps`: reads a first-person sector controller from `target_entity` and
  renders from that actor's eye position. If the controller has not updated
  yet, the camera falls back to the actor's `yaw`, `pitch`, and `view_smooth`
  properties. The controller also publishes a normalized view direction to
  `camera_forward` by default; override this with `forward_property` when a
  game wants a different property name for projectile or camera logic. If
  omitted, FOV defaults to 90 degrees on the vertical axis.
- `adapter`: delegates camera ownership to a native or Lua adapter.

Perspective, chase, and FPS cameras can be tuned at runtime with scene state:

```json
{
  "name": "camera.player",
  "type": "fps",
  "target_entity": "entity.player",
  "fov": 90.0,
  "fov_axis": "horizontal",
  "fov_key": "settings.camera.fov",
  "fov_axis_key": "settings.camera.fov_axis"
}
```

`fov_key` reads a numeric scene-state value. `fov_axis_key` reads a string scene
state value and accepts `vertical` or `horizontal`. Invalid runtime values fall
back to the authored camera values. Use horizontal FOV for FPS-style settings
when designers want values like "90 degrees" to mean the player's visible
left-to-right angle; at 16:9, a 90-degree horizontal FOV renders with a vertical
FOV of roughly 58.7 degrees.

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
    "units": "meters",
    "meters_per_unit": 1.0,
    "axes": { "horizontal": "x", "vertical": "y", "depth": "z" },
    "bounds": { "min": [-9.0, -5.0, -1.0], "max": [9.0, 5.0, 2.0] },
    "cameras": [],
    "lights": []
  }
}
```

SDL3D's default world convention is one authored world unit equals one meter
(`units: "meters"`, `meters_per_unit: 1.0`). Author these fields explicitly in
new games and demos so editors, physics tuning, movement speeds, and level
metrics share the same scale vocabulary.

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
    "skybox": {
      "pos_x": "image.sky.px",
      "neg_x": "image.sky.nx",
      "pos_y": "image.sky.py",
      "neg_y": "image.sky.ny",
      "pos_z": "image.sky.pz",
      "neg_z": "image.sky.nz",
      "size": 400.0
    },
    "sector_levels": [
      {
        "level": "sector.e1m1",
        "variant": "lightmapped",
        "variant_key": "render_sector_variant",
        "position": [0.0, 0.0, 0.0],
        "portal_culling": true,
        "portal_culling_key": "render_portal_culling"
      }
    ]
  }
}
```

`world.skybox` is optional and references six `assets.images` entries using
standard cubemap face names: `pos_x`, `neg_x`, `pos_y`, `neg_y`, `pos_z`, and
`neg_z`. The generic presentation layer draws it before sector levels and other
world primitives. Use `asset://` image paths for pack-file and embedded builds.

`level` references a top-level sector level. `variant` defaults to
`lightmapped` and may be `lightmapped`, `vertex_baked`, or `unlit`.
`position` defaults to the origin and translates the level as a whole.
`portal_culling` defaults to `true`; when enabled, generic presentation
computes visibility from the active camera before drawing the level.
`variant_key` and `portal_culling_key` are optional scene-state keys. When
present, they override `variant` and `portal_culling` at runtime. This supports
data-authored debug/profile controls without custom host-side input code.

Renderer, controller, and editor phases should consume runtime descriptors
instead of parsing JSON directly. `world.kind` remains a high-level statement
of spatial intent; `sector_levels` is the concrete sector-world data.

## Sector Navigation

`sector_navigation` declares reusable navigation graphs over authored
`sector_levels`. The graph is intentionally explicit: authors decide which
sector anchors are connected, which keeps AI pathing structurally correct even
when the rendered sector mesh is complex.

```json
{
  "sector_navigation": [
    {
      "name": "nav.level_1",
      "sector_level": "sector.level_1",
      "nodes": [
        { "name": "start", "sector": "start_room", "position": [5.0, 1.0, 4.0] },
        { "name": "hall", "sector": "north_hall", "position": [12.0, 1.0, 4.0] },
        { "name": "arena", "sector": "arena", "position": [22.0, 1.0, 10.0] }
      ],
      "links": [
        { "from": "start", "to": "hall" },
        { "from": "hall", "to": "arena", "cost": 3.5 }
      ]
    }
  ]
}
```

Validation requires unique graph and node names, a valid `sector_level`, a
non-empty `nodes` array, exact vec3 node `position` values, exactly one of
`sector` or `sector_index` per node, link endpoints that reference declared
nodes, optional boolean `bidirectional`, and positive optional `cost`. Links are
bidirectional by default. When `cost` is omitted, runtime path queries use the
world-space distance between linked nodes.

Lua and C runtime helpers can query the nearest node, path availability, full
node paths, or the next node toward a goal. Nearest-node lookup first prefers
nodes in the sector containing the query position, then falls back to the
nearest graph node. Keep dense per-frame steering in engine components or
lightweight Lua; use sector navigation for high-level decisions such as patrol
routes, chase goals, retreat points, and encounter scripting.

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

Generic actor interaction uses an `interactable` component and the
`interaction.use` action. Prefer this for switches, levers, consoles, pickup
stations, keyed locks, and actor-driven world triggers. Sector doors can still
use `sector_door.*` actions inside the interactable's action list:

```json
{
  "name": "entity.security_switch",
  "tags": ["usable"],
  "transform": { "position": [4.0, 1.0, 8.0] },
  "properties": {
    "interaction_cooldown": { "type": "float", "value": 0.0 }
  },
  "components": [
    {
      "type": "interactable",
      "prompt_key": "prompt.use.security_switch",
      "range": 2.0,
      "min_dot": 0.2,
      "cooldown_property": "interaction_cooldown",
      "cooldown": 0.25,
      "requires": { "property": "blue_key", "amount": 1, "consume": false },
      "actions": [
        { "type": "sector_door.open", "target": "door.security" }
      ],
      "locked_actions": [
        { "type": "scene_state.set", "key": "prompt", "value": "Needs blue key" }
      ],
      "cooldown_actions": [
        { "type": "scene_state.set", "key": "prompt", "value": "Wait..." }
      ]
    }
  ]
}
```

```json
{ "type": "interaction.use", "actor": "entity.player", "target_tag": "usable" }
```

`interaction.use` finds the nearest active actor with an `interactable`
component in range and in front of the source actor. Yaw defaults to the
source actor's `yaw` property; override it with `yaw_property`. `range` and
`min_dot` on the action override component defaults for that use attempt. If no
target is found, `miss_actions` may run; otherwise the action is a successful
no-op.

Interaction payloads include `actor_name`, `source_actor_name`,
`interactable_actor_name`, `target_actor_name`, `prompt_key`,
`interaction_status`, `locked`, and `distance`. Requirements read numeric
properties from the source actor. Use `consume: true` for one-shot keys,
currency, or charges. `cooldown_property` is stored on the interactable and is
decremented by the component while the actor is active.

`sector_platforms` move authored sector floors over time and rebuild sector
render/collision variants when the movement exceeds `rebuild_min_delta`. Use
them for elevators, lifts, moving bridges, and other deterministic sector
geometry motion:

```json
{
  "sector_platforms": [
    {
      "name": "platform.lift",
      "scene": "scene.level_1",
      "sector_level": "sector.level_1",
      "sector": "lift",
      "min_floor_y": 0.0,
      "max_floor_y": 2.5,
      "ceil_y": 12.0,
      "cycle_seconds": 6.0,
      "rebuild_min_delta": 0.02,
      "crush_damage_per_second": 20.0,
      "crush_clearance": 1.6,
      "crush_actor_tag": "player",
      "damage_type": "crush",
      "crush_actions": [
        {
          "type": "property.set",
          "target_from_payload": "actor_name",
          "key": "last_hazard",
          "value_from_payload": "sector_platform"
        }
      ]
    }
  ]
}
```

Use either `sector` or `sector_index` to choose the sector. `ceil_y` must stay
above `max_floor_y`, `cycle_seconds` must be positive, and `scene` may be
omitted for a platform that updates in every scene. The platform follows a
smooth sine cycle that starts at `min_floor_y`, rises to `max_floor_y`, and
returns to `min_floor_y`.

Platforms can also act as moving world hazards. `crush_damage_per_second`
applies generic combat damage while the platform moves upward into an actor's
`crush_clearance` inside the platform sector. Use `crush_actor_tag` (or
`actor_tag`) to limit affected actors. Without a tag, every active actor in the
active scene is eligible. By default, damage only applies while the platform is
rising; set `crush_when_descending: true` for moving ceilings, crushers, or
other authored setups that should also damage on downward motion.

Crush damage uses the same property conventions as `combat.damage`, including
`health_property`, `max_health_property`, `armor_property`,
`armor_absorb_property`, `alive_property`, `damage_type`, `on_damage`, and
`on_death`. `crush_actions` run for each crushed actor with payload fields:
`actor_name`, `target_actor_name`, `sector_platform`, `sector_level`,
`sector_index`, `sector_platform_floor_y`, `sector_platform_floor_delta`,
`sector_platform_crush_damage`, and `amount`. This is intended for feedback
such as sparks, warning lights, sounds, or diagnostic UI.

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

FPS sector controllers also expose reusable actions for trigger volumes and
scripted events:

```json
{
  "type": "controller.fps_sector.launch",
  "target": "entity.player",
  "vertical_velocity": 15.0
}
```

`controller.fps_sector.launch` applies an upward impulse to the controller and
requires a positive `vertical_velocity`.

```json
{
  "type": "controller.fps_sector.teleport",
  "target": "entity.player",
  "position": [72.0, 4.1, 63.0],
  "yaw": -1.5707964,
  "pitch": 0.0
}
```

`controller.fps_sector.teleport` moves the controller and actor to the given
eye-position. `yaw` and `pitch` are optional; omitted angles preserve the
current orientation. Both actions also support `target_from_payload` instead of
`target` when a sensor payload supplies the actor name.

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

Scene `input` can restrict allowed actions and request relative mouse capture:

```json
{
  "input": {
    "mouse_capture": "unpaused",
    "actions": ["action.move.forward", "action.look", "action.pause"]
  }
}
```

`input.mouse_capture` accepts `never`, `unpaused`, or `always`. Missing policy
defaults to `never`. FPS-style scenes should normally use `unpaused` so the
generic runtime captures relative mouse motion during play and releases it
while authored pause/menu overlays are active.

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

## Actor Archetypes, Instances, And Pools

`actor_archetypes` describe reusable actor templates. `actor_instances` place
static actors from those templates at composition time, while `actor_pools`
preallocate runtime spawn/despawn actors from the same template shape.

Placed instances are useful for crates, lamps, pickups, enemies, decorations,
and other authored actors that repeat across a level:

```json
{
  "actor_archetypes": [
    {
      "name": "archetype.crate",
      "active": true,
      "tags": ["prop", "crate"],
      "components": [
        { "type": "render.cube", "size": [1.0, 1.0, 1.0], "texture": "image.crate" }
      ]
    }
  ],
  "actor_instances": [
    {
      "name": "entity.crate.storage.0",
      "archetype": "archetype.crate",
      "transform": { "position": [14.0, 0.5, 20.0] }
    }
  ]
}
```

Instances expand into `entities` before validation. The instance object is a
recursive override over the archetype: object fields merge, and scalar/array
fields replace the archetype value. Generated actors are ordinary entities for
scene membership, sensors, render primitives, Lua lookup, and action targets.

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

`entities`, `actor_archetypes`, `actor_instances`, and `actor_pools` may carry
an optional `editor` object. The runtime ignores this metadata during gameplay;
it exists for editors, dojos, palette browsers, prefab previews, and validation.

```json
{
  "name": "archetype.jump_pad",
  "tags": ["mechanic", "jump_pad"],
  "editor": {
    "display_name": "Jump Pad",
    "category": "mechanics/movement",
    "tags": ["launch", "trigger"],
    "preview": { "primitive": "cube", "color": [0.25, 0.85, 1.0, 1.0] },
    "bounds": { "size": [2.0, 0.2, 2.0] },
    "snap": { "grid": 0.5, "align_to_floor": true },
    "exposed_properties": [
      { "name": "vertical_velocity", "type": "float", "display_name": "Launch Velocity", "min": 0.1 }
    ],
    "test_scene": "scene.dojo.fps_world"
  }
}
```

Supported editor fields:

- `display_name`, `description`, `category`, `icon`, and `preview_asset`:
  non-empty strings when present
- `tags`: array of non-empty strings
- `preview`: object with optional `primitive`, `asset`, and `color`; primitive
  may be `none`, `cube`, `sphere`, `capsule`, `sprite`, `model`, `light`,
  `volume`, or `sector`
- `bounds`: optional `center`, positive `size`, positive `half_extents`, or
  positive `radius`
- `snap`: optional positive `grid` number or vec3, positive
  `rotation_degrees`, and boolean `align_to_floor`
- `exposed_properties`: tool-facing property controls with unique `name`,
  optional display text, optional `type` (`bool`, `int`, `float`, `string`,
  `vec2`, `vec3`, `color`, or `enum`), numeric `min`/`max`, and scalar/vector
  default matching the declared type; `color` defaults may be RGB or RGBA
- `test_scene`: optional scene reference that tools can launch through the
  runner for isolated tuning

The root `editor` object may also declare reusable `templates`. Template
entries use the same fields plus required `name`, and optional `source` /
`source_kind` strings that identify the authored object or fragment a tool
should copy or instantiate. This is intentionally metadata only; copying,
placement, and editing behavior belongs to tools.

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
  "velocity_from_property": "camera_forward",
  "speed": 12.0,
  "cooldown_property": "fire_timer",
  "properties": { "owner": 1, "damage": 1 }
}
```

The target actor may define `fire_cooldown`; otherwise the action's `cooldown`
field is used. If the cooldown property is positive, no projectile is spawned.
Use `velocity` for a literal projectile velocity, or
`velocity_from_property` plus `speed` to fire along an actor-authored direction
such as an FPS controller's `camera_forward`. Use `target_from_payload` instead
of `target` when the firing actor should come from a signal or collision
payload.

`weapon.projectile` is the component form for actors that fire while an input
action is held:

```json
{
  "type": "weapon.projectile",
  "action": "action.fire",
  "pool": "pool.player_shots",
  "offset": [0.0, 0.1, -0.5],
  "velocity_from_property": "camera_forward",
  "speed": 20.0,
  "cooldown_property": "fire_timer"
}
```

Unlike `projectile.fire`, the component fires from its owning actor and does
not accept `target` or `target_from_payload`. This is the preferred shape for
FPS and shooter player weapons because it keeps input-to-projectile behavior
declarative.

Projectiles can also participate in the generic weapon state convention. Add
`clip_property` to consume from a magazine, or `ammo_resource` / `ammo_property`
to consume directly from a numeric resource. `ammo_per_shot` defaults to `1`.
`reload_timer_property` prevents firing while a reload is pending.
`cooldown_property` defaults to `fire_timer`, preserving the older projectile
cooldown behavior. Optional `on_fire`, `on_empty`, `on_cooldown`, and
`on_reloading` signals receive `actor_name`, `source_actor_name`,
`weapon_status`, and `ammo_per_shot`.

`weapon.state` completes data-authored reloads and can decay a weapon cooldown:

```json
{
  "type": "weapon.state",
  "clip_property": "clip",
  "clip_size_property": "clip_size",
  "reserve_property": "ammo_reserve",
  "reload_timer_property": "reload_timer",
  "reload_pending_property": "reload_pending",
  "cooldown_property": "fire_timer",
  "cooldown_rate": 1.0
}
```

Start a reload with `weapon.reload`:

```json
{
  "type": "weapon.reload",
  "target": "entity.player",
  "clip_property": "clip",
  "clip_size_property": "clip_size",
  "reserve_property": "ammo_reserve",
  "reload_seconds": 0.8
}
```

When the timer reaches zero, `weapon.state` fills the clip from the reserve and
clears `reload_pending`. If `consume_reserve` is `false`, the clip fills
without reducing reserve ammo, which is useful for energy weapons or prototypes.

`weapon.hitscan` traces instantly from an actor. It can trace against sector
geometry through `sector_level`, select the nearest active actor with
`target_tag`, consume the same clip/ammo fields as projectiles, and run authored
actions with a payload:

```json
{
  "type": "weapon.hitscan",
  "target": "entity.player",
  "sector_level": "sector.e1m1",
  "direction_from_property": "camera_forward",
  "target_tag": "enemy",
  "range": 64.0,
  "clip_property": "clip",
  "actions": [
    {
      "type": "combat.damage",
      "target_from_payload": "actor_name",
      "source_from_payload": "source_actor_name",
      "amount": 15.0,
      "damage_type": "hitscan"
    }
  ]
}
```

Hitscan payloads include `source_actor_name`, `actor_name` / `hit_actor_name`,
`origin`, `direction`, `hit_position`, `hit_distance`, `hit_actor`, and
`hit_wall`. Use these fields for damage, particles, lights, sounds, decals, or
debug UI. Actor hit tests use a target actor's `hit_radius` or `radius`
property, falling back to the action's `hit_radius` value.

## Sector Level Fragments

Large sector worlds can be split by authoring concern without duplicating a
full `sector_levels` object in every file:

```json
{
  "schema": "sdl3d.fragment.v0",
  "sector_level_fragments": [
    {
      "level": "sector.e1m1",
      "materials": [{ "name": "floor", "texture": "asset://textures/floor.png" }]
    }
  ]
}
```

Each `sector_level_fragments` entry names a target `level` and may append
`materials`, `sectors`, and/or `lights`. Composition creates or finds the named
sector level, appends those arrays in import order, then the normal
`sector_levels` validator checks material references, duplicate sectors,
geometry, and light shape. Use this for files such as
`world/e1m1.materials.json`, `world/e1m1.rooms.json`, and
`world/e1m1.lights.json`. Keep each fragment logically cohesive; do not scatter
one room across unrelated files unless an editor owns that layout.

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
- `motion.sector_velocity_3d`: moves an actor by a `vec3` velocity property
  through an authored `sector_level`, tracing against sector volumes and
  despawning on impact by default. Use this for data-authored projectiles in
  FPS/sector worlds.
- `motion.patrol`: moves an actor through an authored list of 3D waypoints.
  Use `speed`, `wait_time`, `arrival_radius`, `mode` (`loop` or `ping_pong`),
  and optional `yaw_property` to expose movement direction to rendering or Lua
  logic.
- `lifecycle.ttl`: increments an age property every update and despawns pooled
  actors once the configured TTL is reached. Use this for short-lived bursts and
  effects instead of scanning active actors in Lua.
- `motion.grid_agent`: moves an actor along an authored `grid_maps` maze with
  queued turns, walkability checks, center snapping, and optional map wrapping.
- `controller.fps_sector`: first-person movement through a sector level using
  authored input actions, sector collision, jumping, gravity, stair stepping,
  and mouse-look.
- `combat.health`: declares that an actor participates in generic combat
  health/armor actions. The actual runtime state remains ordinary actor
  properties so UI, Lua, replication, saves, and data actions can read the same
  values. Use properties such as `health`, `max_health`, `armor`,
  `armor_absorb`, and `alive` for defaults.
- `pickup.respawn`: reactivates an inactive pickup actor after a respawn timer
  reaches zero. By default it reads `pickup_respawn_remaining` and writes
  `pickup_available`.
- `status_effect.timer`: expires a temporary property-driven status effect.
  It decrements a duration property such as `quad_damage_remaining`, writes an
  `expired_value` when the timer reaches zero, and can clear an active flag.
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
  named by `size_property`. `texture` may reference an image asset id; each cube
  face is UV-mapped to the full image and tinted by `color`. The property path
  is useful for grid wall runs and other pooled actors that need per-instance
  dimensions.
- `render.mesh_primitive`: declares a procedural placeholder mesh descriptor.
  The canonical `primitive` values are `cube`, `sphere`, `capsule`,
  `cylinder`, `cone`, `torus`, `pyramid`, and `wedge`. These descriptors use
  the same transform, `color`, `texture`, `lighting`, and `emissive` fields as
  other render primitives where applicable, and add `draw_mode`: `solid`
  (default), `wire`, or `solid_wire`. Dimension fields include `size`,
  `radius`, `height`, `radius_top`, `radius_bottom`, `major_radius`,
  `minor_radius`, `segments`/`slices`, `rings`, and `tube_segments`. This is the
  stable data/runtime representation for procedural mesh primitives. The
  generic presentation path renders every primitive as shaded solid geometry by
  default, with optional wire-only or solid-plus-wire modes.
- `render.model`: renders an authored `assets.models` entry with `model`,
  optional `scale`, axis-angle rotation, `color` tint, and optional skeletal
  animation playback via `animation_clip` and `animation_time_property`.
  `animation_loop` defaults to `true` and wraps authored animation time by the
  selected clip duration.
- `render.sprite`: renders an upright billboard using an authored sprite asset.
  Use `size` for world-space width/height and optional `facing_yaw` or
  `facing_yaw_property` for directional sprite frame selection. Sprite assets
  participate in dynamic lighting when their asset and component both leave
  lighting enabled.

Sprite assets can be authored as either sheet assets or explicit file lists.
Sheet assets omit `kind` or set `"kind": "sheet"` and provide `path`,
`frame_width`, `frame_height`, `columns`, `rows`, `frame_count`, and
`direction_count`. File-list assets use `"kind": "files"` with `base_paths`
containing one fallback image per direction and `frame_paths` containing
animation frames in frame-major, direction-minor order. File-list assets are
useful when tools export one image per frame/direction instead of an atlas.

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

Scene-state actions write transient values that UI, render settings, sector
instances, and conditions can read:

```json
{ "type": "scene_state.set", "key": "render_profile", "value": "ps1" }
{ "type": "scene_state.toggle", "key": "render_lighting_enabled", "default": true }
{
  "type": "scene_state.cycle",
  "key": "sector_variant",
  "default": "lightmapped",
  "values": ["lightmapped", "vertex_baked", "unlit"]
}
```

Use `set` for fixed values, `toggle` for booleans, and `cycle` for small
ordered sets such as render profiles or debug variants.

UI text bindings can read scene state with an optional scalar `default`, which
keeps HUD text renderable before the first action writes a transient value:

```json
{
  "format": "PROFILE %s",
  "bindings": [{ "type": "scene_state", "key": "render_profile", "default": "modern" }]
}
```

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

Generic combat actions provide reusable health, armor, death, and revival
behavior without writing game-specific C:

```json
{
  "type": "combat.damage",
  "target": "entity.enemy",
  "source": "entity.player",
  "amount": 35.0,
  "damage_type": "slash",
  "on_damage": "signal.enemy.damaged",
  "on_death": "signal.enemy.killed"
}
```

`combat.damage` reads `health`, `max_health`, `armor`, `armor_absorb`, and
`alive` by default. `armor_absorb` is clamped to 0..1; absorbed damage depletes
armor first, and the remainder reduces health. Health is clamped at zero. The
death signal is emitted only when the action transitions an actor from alive to
dead. Set `deactivate_on_death: true` when the actor should stop rendering and
updating immediately.

`combat.heal`, `combat.kill`, and `combat.revive` use the same target and
property conventions:

```json
{ "type": "combat.heal", "target": "entity.player", "amount": 25.0 }
{ "type": "combat.kill", "target_from_payload": "actor_name", "on_death": "signal.actor.killed" }
{ "type": "combat.revive", "target": "entity.player", "health": 50.0, "on_revive": "signal.player.revived" }
```

All combat actions support `target_from_payload`, optional `source` or
`source_from_payload`, and property-name overrides such as `health_property`,
`max_health_property`, `armor_property`, `armor_absorb_property`, and
`alive_property`. Damage/heal amount can come from `amount_from_payload`, and
revive health can come from `health_from_payload`. Combat event payloads include
`actor_name`, `source_actor_name`, `damage_type`, `amount`, `armor_delta`,
`health_delta`, `health`, `max_health`, `armor`, `alive`, and `dead`.

Prefer combat actions over raw `property.add` for gameplay health because they
centralize armor absorption, clamping, alive/dead state, and signal emission.
Raw property actions remain useful for simple meters, counters, and diagnostic
state.

### Factions And Target Filters

Games can author a reusable faction relationship matrix at the root:

```json
{
  "factions": {
    "player": { "player": "friendly", "monster": "hostile", "neutral": "neutral" },
    "monster": { "player": "hostile", "monster": "friendly", "neutral": "neutral" },
    "neutral": { "player": "neutral", "monster": "neutral", "neutral": "friendly" }
  }
}
```

Actors participate by storing a string property, `faction` by default. If no
matrix entry exists, actors in the same faction are treated as `friendly`,
actors in different non-empty factions are treated as `hostile`, and missing
factions are treated as `neutral`.

Combat damage, hitscan weapons, explosions, contact/sector/volume sensors, and
sector platform crush hazards accept `target_filter`:

```json
{
  "type": "effect.explosion",
  "source": "entity.player_rocket",
  "radius": 4.0,
  "damage": 45.0,
  "target_filter": {
    "relationship": "hostile",
    "include_tags": ["combatant"],
    "exclude_owner": true,
    "owner_property": "owner",
    "exclude_source": true
  }
}
```

Supported filters include `relationship` (`any`, `friendly`, `hostile`,
`neutral`, or `ignored`), `target_faction`, `include_factions`,
`exclude_factions`, `target_tag`, `include_tags`, `exclude_tags`,
`exclude_source` / `exclude_self`, `exclude_owner`, `owner_property`,
`faction_property`, `source_faction`, `source_faction_from_payload`, and
`source_faction_property`. String-list fields accept either one string or an
array of strings.

Use filters instead of duplicating tag-only logic. They keep friendly fire,
trap ownership, neutral NPCs, projectile owners, and world-owned hazards
data-authored and consistent across combat, sensors, and effects.

`effect.explosion` applies an authored radial effect around a position or actor.
It is suitable for grenades, exploding barrels, blast spells, mines, traps, and
chain reactions:

```json
{
  "type": "effect.explosion",
  "source": "entity.player",
  "radius": 5.0,
  "inner_radius": 1.0,
  "damage": 40.0,
  "damage_type": "blast",
  "target_tag": "enemy",
  "impulse": 2.0,
  "actions": [
    {
      "type": "property.set",
      "target_from_payload": "actor_name",
      "key": "last_blast_falloff",
      "value_from_payload": "falloff"
    }
  ]
}
```

The origin is resolved from `position`, `from` / `from_payload`, or
`source` / `source_from_payload`, then `offset` is added when present. `radius`
is required; `inner_radius` receives full effect before falloff begins.
`falloff` defaults to `linear`; use `constant` or `none` for full-strength
damage throughout the radius. `target_tag` (or `affected_tag`) limits affected
actors to authored entity/archetype tags. Without a tag, every active actor in
the active scene is considered. `exclude_source` defaults to true so a blast
does not damage the actor that emitted it unless explicitly requested.

`damage` (or the equivalent `amount`) uses the same health, armor, alive/dead,
source, damage-type, and signal conventions as `combat.damage`. `impulse`
adds radial velocity to the target actor's `velocity` property by default; use
`velocity_property` to write a different Vec3 property. `max_targets` can cap
the number of affected actors for deterministic bounded chain reactions.

For each affected actor, nested `actions` run with a payload containing
`actor_name`, `target_actor_name`, `source_actor_name`, `origin`, `distance`,
`radius`, `falloff`, `damage`, `amount`, and `impulse`. Nested actions can
spawn pooled visual effects, despawn hit actors, or trigger another
`effect.explosion` from `from_payload: "actor_name"` to model chain reactions.
The optional `on_hit` signal receives the same payload.

### Resources, Pickups, Stations, And Status Effects

Generic resource actions manage numeric meters such as ammo, mana, keys, fuel,
stamina, shields, or currency. They operate on ordinary actor properties so UI,
Lua, persistence, and networking see the same state.

```json
{ "type": "resource.add", "target": "entity.player", "resource": "ammo", "amount": 12 }
{ "type": "resource.consume", "target": "entity.player", "resource": "ammo", "amount": 1 }
{ "type": "resource.set", "target": "entity.player", "resource": "stamina", "value": 100 }
```

By default, `resource: "ammo"` reads and writes the `ammo` property and clamps
against `max_ammo` when present. Override names with `property` and
`max_property` when a project uses different naming. `amount_from_payload` and
`value_from_payload` allow sensors or other actions to drive the value. Consume
actions are non-destructive on failure unless `allow_partial: true`; use
`on_success` and `on_failure` to emit authored feedback signals.

`pickup.collect` grants one or more resources to a collector actor, optionally
deactivates the pickup, and can start a respawn timer:

```json
{
  "type": "pickup.collect",
  "target_from_payload": "actor_name",
  "pickup_from_payload": "other_actor_name",
  "respawn_seconds": 15.0,
  "resources": [
    { "resource": "health", "amount": 25.0 },
    { "resource": "ammo", "amount": 8.0 }
  ],
  "on_collected": "signal.pickup.collected"
}
```

Author a `pickup.respawn` component on the pickup actor when it should become
active again after `pickup_respawn_remaining` reaches zero. This component runs
for inactive actors in the active scene, so respawned pickups do not require Lua
polling.

Resource stations are reusable actors such as health fountains, ammo terminals,
and recharge pads. `resource.station.use` grants resources if the station has
charges and is off cooldown:

```json
{
  "type": "resource.station.use",
  "target": "entity.player",
  "station": "entity.health_station",
  "cooldown": 2.0,
  "resources": [{ "resource": "health", "amount": 20.0 }]
}
```

The station reads `charges` and `cooldown` by default. Add a `property.decay`
component for the cooldown property so it counts down over time. Set
`consume_charge: false` for infinite stations.

Temporary power-ups can be modeled with `status_effect.apply` and
`status_effect.timer`:

```json
{
  "type": "status_effect.apply",
  "target": "entity.player",
  "property": "quad_damage",
  "value": true,
  "duration": 10.0,
  "duration_property": "quad_damage_remaining",
  "active_property": "quad_damage_active"
}
```

The matching component resets the property when the timer expires:

```json
{
  "type": "status_effect.timer",
  "property": "quad_damage",
  "duration_property": "quad_damage_remaining",
  "active_property": "quad_damage_active",
  "expired_value": false
}
```

Use these primitives for reusable mechanics. Put game-specific policy, such as
which pickups spawn in a level or how a power-up changes enemy AI, in JSON and
Lua.

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
can be authored without Lua. `ambient_sound_id` can drive authored ambient-zone
audio with `audio.set_ambient`:

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

```json
{
  "name": "sensor.upper_deck.ambient",
  "type": "sensor.sector",
  "actor": "entity.player",
  "sector_level": "sector.doom.e1m1",
  "sector": "upper_deck",
  "edge": "enter",
  "actions": [
    { "type": "audio.set_ambient", "ambient_id_from_payload": "ambient_sound_id", "fade": 1.0 }
  ]
}
```

Use `actor_tag` instead of `actor` to apply a sector sensor to every active
actor with a matching tag. `sector_index` may be used instead of `sector` when
the authored sector order is deliberate and stable.

`sensor.perception` detects whether an observer can see one or more targets
inside a sector level. It combines range, field-of-view, sector line-of-sight,
and the standard `target_filter` object, so faction-aware AI perception can be
authored without game-specific C. Use exactly one of `observer` or
`observer_tag`, exactly one of `target` or `target_tag`, and a `sector_level`.
The sensor supports `enter`, `stay` / `overlap`, and `exit` edges.

```json
{
  "name": "sensor.guard.sees_enemy",
  "type": "sensor.perception",
  "observer": "entity.guard",
  "target_tag": "actor",
  "sector_level": "sector.dungeon",
  "range": 18.0,
  "fov_degrees": 120.0,
  "observer_eye_height": 1.5,
  "target_eye_height": 1.0,
  "target_filter": { "relationship": "hostile" },
  "edge": "stay",
  "actions": [
    { "type": "property.set", "target": "entity.guard", "key": "alerted", "value": true },
    { "type": "property.set", "target_from_payload": "target_actor_name", "key": "was_seen", "value": true }
  ]
}
```

Use `min_dot` instead of `fov_degrees` when you want to author the dot-product
threshold directly; `-1.0` means omnidirectional. `yaw_property` defaults to
`yaw` and is used for field-of-view checks. The payload includes `actor_name`
and `observer_actor_name` for the observer, `target_actor_name` and
`other_actor_name` for the seen target, `sector_level`, `distance`,
`perception_distance`, `range`, `origin`, and `target_position`.

`noise.emit` creates a short-lived runtime noise event. It does not play audio
by itself; pair it with `audio.play_sfx` when the game should also make an
audible sound. A noise can come from `source`, `actor`, `target`, the matching
`*_from_payload` fields, `from`, `from_payload`, or a literal `position` plus
optional `offset`.

```json
{
  "type": "noise.emit",
  "source": "entity.imp",
  "radius": 14.0,
  "loudness": 1.0,
  "duration": 0.25
}
```

`sensor.hearing` lets actors react to those noise events. Use exactly one of
`actor` or `actor_tag` to select listeners. `range` caps the listener's hearing
range; the effective hearing radius is the smaller of the sensor range and the
noise radius. Use `target_filter` to restrict the source actor by tag, owner, or
faction relationship.

```json
{
  "name": "sensor.guard.hears_hostile",
  "type": "sensor.hearing",
  "actor": "entity.guard",
  "target_tag": "noisy",
  "range": 18.0,
  "target_filter": { "relationship": "hostile" },
  "edge": "enter",
  "actions": [
    { "type": "property.set", "target": "entity.guard", "key": "alerted", "value": true },
    { "type": "property.set", "target": "entity.guard", "key": "last_noise", "value_from_payload": "noise_position" }
  ]
}
```

The hearing payload includes `actor_name` and `listener_actor_name` for the
listener, `source_actor_name`, `target_actor_name` when the noise source is an
actor, `noise_id`, `noise_position`, `noise_radius`, `noise_loudness`,
`distance`, `hearing_distance`, `audibility`, and `range`. `audibility` is
linearly attenuated from the event loudness at the source to zero at the
effective radius.

`sensor.volume` detects whether an actor is inside an authored axis-aligned 3D
box. It supports the same `enter`, `stay` / `overlap`, and `exit` edges and
publishes `actor_name` to actions or signals. Use it for trigger volumes such
as camera zones, pickups, checkpoints, and area-specific effects:

```json
{
  "name": "sensor.camera_zone.enter",
  "type": "sensor.volume",
  "actor": "entity.player",
  "min": [41.8, -0.1, 87.8],
  "max": [44.2, 2.0, 90.2],
  "edge": "enter",
  "actions": [
    { "type": "camera.set", "camera": "camera.surveillance" }
  ]
}
```

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
- editor metadata shape, preview hints, exposed properties, and test-scene
  references
- network protocol, replication fields, control messages, runtime bindings,
  directions, schema hash shape, and managed session-flow metadata

Validation warnings are non-fatal by default. Tools can set
`treat_warnings_as_errors` in `sdl3d_game_data_validation_options` for stricter
authoring.
