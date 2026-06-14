# Slayer 3D Game Data JSON

Slayer 3D game data is a JSON-family authoring format for data-driven games. It is
designed so game projects can compose reusable engine primitives in JSON, keep
game-specific rules in Lua, and run through the generic `slayer3d_runner`.

The root schema is currently `slayer3d.game.v0`. Fragment files use
`slayer3d.fragment.v0`.

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
| `schema` | yes | Root schema id, currently `slayer3d.game.v0`. |
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
| `brush_worlds` | no | Native convex-brush worlds for true 3D FPS-style spaces. |
| `editor_brush_sources` | no | Canonical fixed-coordinate editor brush sources used by level-editing tools before runtime compilation. |
| `editor_player_starts` | no | Editor-authored player spawn markers for test-run workflows and level-editing tools. |
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
    "bloom_key": "render_bloom_enabled",
    "ssao": true,
    "depth_prepass": false,
    "depth_prepass_key": "render_depth_prepass_enabled",
    "per_object_light_selection": false,
    "per_object_light_selection_key": "render_per_object_light_selection_enabled",
    "per_object_light_limit": 8,
    "per_object_light_limit_key": "render_per_object_light_limit",
    "procedural_lod": true,
    "procedural_lod_key": "render_procedural_lod_enabled",
    "procedural_lod_near_pixels": 128.0,
    "procedural_lod_far_pixels": 24.0,
    "procedural_lod_min_segments": 8,
    "model_lod_culling": true,
    "model_lod_cull_pixels": 4.0,
    "performance_queries": false,
    "performance_queries_key": "render_performance_queries_enabled",
    "world_render_scale": 1.0,
    "world_render_scale_key": "render_world_scale",
    "quality": "quality",
    "quality_key": "render_quality",
    "quality_presets": [
      { "name": "performance", "label": "Performance", "world_render_scale": 0.5, "procedural_lod": true },
      { "name": "quality", "label": "Quality", "world_render_scale": 1.0, "procedural_lod": false }
    ],
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
`depth_prepass` enables an OpenGL opaque depth pre-pass for eligible static lit
triangle meshes. The data-game runtime currently opts brush-world meshes into
this pass and excludes actors, viewmodels, particles, billboards, and immediate
debug primitives. This can reduce expensive fragment shading in dense brush
scenes with overdraw, especially when many dynamic lights are active. It adds an
extra position-only replay of eligible opaque geometry, so leave it disabled for
very simple or CPU-bound scenes unless profiling shows a benefit.
`per_object_light_selection` lets capable renderers keep a larger scene-level
candidate light set, then upload only the most relevant lights for each lit draw.
`per_object_light_limit` is clamped to the backend shader capacity, currently 8.
The selector always preserves directional lights as high-priority candidates and
ranks point/spot lights by draw bounds, light range, intensity, and distance.
This is useful in scenes with many localized dynamic lights: shader work scales
with the authored per-object limit instead of total active scene lights. Optional
`*_key` fields allow data-authored debug toggles and A/B tests without host C.
`procedural_lod` allows the presentation layer to reduce tessellation for
eligible procedural geometry when it projects to a small screen area.
`procedural_lod_near_pixels` is the projected diameter at or above which the
authored tessellation is preserved, `procedural_lod_far_pixels` is the projected
diameter at or below which the minimum tessellation is used, and
`procedural_lod_min_segments` is the lowest generated segment/ring count. The
far threshold must be less than or equal to the near threshold. LOD is skipped
for camera-space viewmodels/effects so first-person weapons and HUD-adjacent
geometry keep stable authored detail. Optional `*_key` fields let debug menus
and tuning scenes toggle the feature or tune thresholds from scene state.
Renderer stats expose candidate, reduced, authored-triangle,
resolved-triangle, and saved-triangle counts so performance dojos can prove the
effect quantitatively instead of relying on visual inspection.
`model_lod_culling` applies the same screen-space policy to authored
`render.model` components by skipping world-space model draws that project below
`model_lod_cull_pixels`. This is a zero-detail LOD level intended for tiny
distant props, pickups, and repeated decorations; leave `lod` disabled on hero
actors, enemies that must remain visible, and inspection objects. Camera-space
viewmodels are never culled by model LOD. Individual `render.model` components
can override the global threshold with `lod_cull_pixels`, and `lod_bias` affects
their projected size. Renderer stats expose model LOD candidate, culled, and
saved-triangle counts.
`performance_queries` enables optional backend sample-count queries for
diagnostic overlays. Capable OpenGL backends expose depth-passing sample counts
for the depth pre-pass and main geometry pass through UI metrics. These queries
can block while reading GPU results, so use them for profiling and do not leave
them enabled in production scenes by default.
`world_render_scale` renders only the 3D/world layer at a lower internal
resolution on capable backends, then upscales it into the normal logical
viewport before drawing UI. Valid values are `0.25` through `1.0`; `1.0`
preserves full logical resolution. UI overlays, menu text, mouse/input mapping,
and camera aspect ratio remain tied to the authored logical resolution, so HUDs
stay sharp even when the world layer is scaled down. Use
`world_render_scale_key` for options menus or profiling scenes that need a
runtime quality slider without host C. The UI metrics namespace exposes
`render.window_pixel_width`, `render.window_pixel_height`, and
`render.window_pixel_density` so profiling screens can verify the actual
platform backing size independently from the authored logical size.
`quality_presets` are named render-setting bundles selected by `quality` or the
scene-state value at `quality_key`. Presets may author the same performance
knobs as the root `render` object, including world scale, depth pre-pass,
per-object light selection, procedural LOD, model LOD culling, and performance
queries. Root settings provide defaults, the selected quality preset overrides
those defaults, and individual `*_key` scene-state values still win last for
fine tuning and debug sliders.

## Structured Imports

Imports are structured composition, not textual includes. Each imported file is
a JSON object with schema `slayer3d.fragment.v0`.

```json
{
  "schema": "slayer3d.game.v0",
  "imports": [
    { "path": "fragments/assets.json", "sections": ["assets"] },
    { "path": "fragments/actors/player.json", "sections": ["entities"] },
    { "path": "fragments/world/e1m1.materials.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/world/e1m1.sectors.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/world/keep_01.brushes.json", "sections": ["brush_worlds"] },
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
  "schema": "slayer3d.fragment.v0",
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
Managed windows request high-DPI backing framebuffers by default when SDL and
the platform support them. Author `"high_pixel_density": false` under
`app.window` only when testing low-DPI presentation behavior or working around a
platform issue. Mouse coordinates and UI layout remain in logical pixels.

`pause.action` toggles the managed runtime pause state when the active scene
allows the action and `pause.allowed_if` is absent or true. `quit.action`
requests shutdown through the managed app flow when `quit.enabled_if` is absent
or true; when `quit.transition` names an authored transition, shutdown is
delayed until that transition finishes. `quit.enabled_if` uses the same data
condition syntax as scene logic, which lets editor shells keep `Esc` local to a
modal palette while the palette is open. Use `app.input_policy.global_actions`
for lifecycle actions such as quit that should work even when a scene restricts
its local `input.actions` list.

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

- `orthographic`: fixed position/target/up with `size`. Optional `size_key`
  reads a positive numeric scene-state value so editor and strategy cameras can
  zoom at runtime without mutating authored camera data.
- `perspective`: position/target/up with `fov` and optional `fov_axis`.
  `position` and `target` may be fixed Vec3 values, or driven by
  `position_entity` / `target_entity` plus optional `position_offset` /
  `target_offset`. Entity-driven targets are useful for panning security
  cameras, rails, and authored cutscene cameras. `fov_axis` may be `vertical`
  or `horizontal`; omitted cameras use vertical FOV. The legacy `fovy` field is
  still accepted as a vertical-FOV alias when `fov` is absent. If omitted, the
  vertical field of view defaults to 90 degrees.
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
back to the authored camera values. Orthographic `size_key` works the same way
for zoomable orthographic cameras. Use horizontal FOV for FPS-style settings
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

Slayer 3D's default world convention is one authored world unit equals one meter
(`units: "meters"`, `meters_per_unit: 1.0`). Author these fields explicitly in
new games and demos so editors, physics tuning, movement speeds, and level
metrics share the same scale vocabulary.

World kinds may include fixed-screen playfields, tile grids, room graphs,
sector/portal maps, brush worlds, or general 3D scenes as engine support
grows.

## Brush Worlds

`brush_worlds` describe native true-3D worlds built from authored convex
brushes. This is the foundation for Quake-like spaces: vertical rooms,
overhangs, ramps, clips, trigger volumes, and future brush collision/render
acceleration. Sector worlds remain supported; choose brush worlds when a level
needs real 3D geometry rather than a sector/portal floor plan.

Brush worlds load, validate, and compile visible brush faces into immutable
static meshes. Collision traces and FPS controller support are layered on top
in later slices.

```json
{
  "brush_worlds": [
    {
      "name": "brush.keep_01",
      "units": "meters",
      "meters_per_unit": 1.0,
      "visibility_cell_size": 2.0,
      "compile": {
        "hidden_face_culling": true,
        "chunk_cell_size": 8.0
      },
      "editor": {
        "stable_id": "brush_world.keep_01.v1",
        "display_name": "Keep 01",
        "category": "levels/brush",
        "snap": { "grid": [0.5, 0.25, 0.5], "rotation_degrees": 15.0 }
      },
      "materials": [
        {
          "name": "stone_wall",
          "texture": "asset://textures/stone_wall.png",
          "albedo": [0.8, 0.8, 0.8, 1.0],
          "emissive": [0.0, 0.0, 0.0],
          "roughness": 0.9,
          "tex_scale": 2.0,
          "editor": {
            "stable_id": "material.keep_01.stone_wall.v1",
            "display_name": "Stone Wall",
            "category": "materials/walls"
          }
        }
      ],
      "brushes": [
        {
          "name": "brush.start_room",
          "tags": ["room", "solid"],
          "contents": ["solid", "player_clip"],
          "visibility": "auto",
          "editor": {
            "stable_id": "brush.keep_01.start_room.v1",
            "display_name": "Start Room",
            "group": "start_area"
          },
          "faces": [
            {
              "plane": { "normal": [ 1,  0,  0], "distance":  8 },
              "material": "stone_wall",
              "uv": { "scale": [1.0, 1.0], "offset": [0.0, 0.0], "rotation_degrees": 0.0 },
              "editor": {
                "stable_id": "face.keep_01.start_room.east.v1",
                "display_name": "East Wall"
              }
            },
            { "plane": { "normal": [-1,  0,  0], "distance":  0 }, "material": "stone_wall" },
            { "plane": { "normal": [ 0,  1,  0], "distance":  4 }, "material": "stone_wall" },
            { "plane": { "normal": [ 0, -1,  0], "distance":  0 }, "material": "stone_wall" },
            { "plane": { "normal": [ 0,  0,  1], "distance":  8 }, "material": "stone_wall" },
            { "plane": { "normal": [ 0,  0, -1], "distance":  0 }, "material": "stone_wall" }
          ]
        }
      ]
    }
  ]
}
```

Validation requires:

- unique brush world names
- `units` omitted or `"meters"`, with positive `meters_per_unit`
- optional positive `visibility_cell_size`, default `2.0`; this controls the
  coarse empty-space grid used by automatic brush visibility culling
- optional `compile` object for runtime/offline compile policy
- optional `compile.hidden_face_culling` boolean, default `true`; when enabled,
  fully covered adjacent opaque solid faces are removed from compiled render
  meshes
- optional positive `compile.chunk_cell_size`; when omitted, the engine derives
  a deterministic chunk size from the world bounds and visibility cell size
- optional `editor` metadata on brush worlds, materials, brushes, and faces;
  `editor.stable_id` values must be unique inside each brush world
- non-empty `materials` with unique names
- material `albedo` as vec3/vec4 values in `[0, 1]`
- non-negative `metallic` and `roughness`
- optional material `emissive` as a non-negative RGB vec3
- positive `tex_scale`
- optional material `texture` as a non-empty existing asset path
- non-empty `brushes` with unique names
- optional unique non-empty brush `tags`
- optional `contents` as one value or an array of unique values: `solid`,
  `player_clip`, `projectile_clip`, `trigger`, `water`, `lava`, or `sky`;
  omitted contents default to `solid`
- optional `visibility` override: `auto` (default), `always`, or `trace`;
  `auto` lets the compiled visibility grid decide, `always` keeps rare
  exception brushes visible, and `trace` enables the older camera-to-brush
  trace fallback when the automatic grid is unavailable
- optional legacy `visibility_cullable` boolean, default `false`; when no
  `visibility` override is authored, `true` is treated as `visibility: "trace"`
- each brush to contain at least four `faces`
- each face to declare `plane.normal` as a non-zero vec3 and
  `plane.distance` as a number
- each face `material` to reference a declared material by name or index
- optional face `uv` object with positive vec2 `scale`, vec2 `offset`, and
  numeric `rotation_degrees`; UV transforms are applied after the material's
  world-unit `tex_scale`
- optional `surface_flags` as one value or an array of unique values:
  `nocollide`, `slick`, `ladder`, `emissive`, or `portal_candidate`

Scenes instantiate brush worlds through `world.brush_worlds`:

```json
{
  "schema": "slayer3d.scene.v0",
  "name": "scene.keep_01",
  "world": {
    "brush_worlds": [
      {
        "world": "brush.keep_01",
        "position": [0.0, 0.0, 0.0],
        "acceleration": true,
        "lighting": true,
        "debug_wireframe": false,
        "visibility_occlusion": false
      }
    ]
  }
}
```

`visibility_occlusion` is an opt-in CPU-side brush visibility pass. At load
time, the engine automatically compiles a conservative empty-space visibility
grid from the brush world's solid/player-clip contents. When a camera is
available, the generic frame path marks empty cells that have unobstructed
grid line-of-sight from the camera and skips brush submodels that have no
neighboring visible cell. This catches common room/corner occlusion without
requiring authors to place portals by hand. It is intentionally fail-open: if
the grid is missing, too large for the current CPU pass, the camera is outside
the grid, or a brush is ambiguous, the brush remains visible. Use
`visibility: "always"` for rare sky, vista, or effect brushes that must never
be culled. Smaller
`visibility_cell_size` values improve blocker precision and doorway behavior at
higher memory/compile cost; larger values are cheaper but more conservative.
Visibility results are cached for a small set of recently visited camera cells,
so normal player movement and debug camera toggles do not force a full visible
cell rebuild every time the view returns to a nearby cell.
Brushes authored with `visibility: "trace"` or legacy
`visibility_cullable: true` still participate in the older trace-based fallback
when no visibility grid is available. `acceleration_key`, `lighting_key`,
`debug_wireframe_key`, and `visibility_occlusion_key` may name scene-state
booleans that override the authored defaults. Runtime and editor code should
use `slayer3d_game_data_get_brush_world()` and
`slayer3d_game_data_for_each_brush_world_instance()` rather than reparsing JSON.
Brush render meshes are available through `brush_world.render_model` and are
drawn by the generic data-game frame path. During load, brush worlds also
batch render-equivalent authored materials into shared static meshes. Materials
remain distinct for editor picking, exports, and face metadata, but materials
with the same texture, color, metallic/roughness, and emissive response can be
submitted as one mesh; per-face UVs still honor each material's `tex_scale` and
face UV overrides. The compile step also conservatively removes fully hidden
contact faces between adjacent opaque solid brushes: a face is culled only when
an opposite coplanar neighboring face fully covers it, so partial overlaps and
ambiguous cases fail open. Runtime descriptors expose
`compile_face_count`, `compile_rendered_face_count`,
`compile_culled_face_count`, `compile_triangle_count`,
`compile_invalid_brush_count`, and `compile_degenerate_face_count` for tests,
editor diagnostics, and performance audits. Brush worlds fail to load when an
authored brush cannot produce bounded geometry; degenerate faces are counted so
tooling can surface geometry-health issues. `compile.hidden_face_culling` can be
disabled by editor/debug content when authors need to compare the source faces
against the optimized render artifact. During load, brush worlds also precompute
local AABBs for each brush and for the whole world, then build spatial compile
chunks from those bounds. `compile.chunk_cell_size` gives editor/offline compile
tools a stable chunking knob; otherwise the runtime computes a conservative
automatic size. Compile chunks preserve authored brushes as the source of truth
while grouping them into optimized runtime broad-phase artifacts for collision
traces, editor diagnostics, and future render/collision chunk generation. The
runtime descriptor also exposes `compile_rendered_faces`, a source-to-render
table for every visible compiled face. Each entry records the source brush,
source face, material, render mesh index, and vertex range, so editor selection,
diagnostics, and offline compilers can map optimized render output back to
stable source brush/face IDs even after hidden internal faces are culled. When a
source face is only partially covered by adjacent structural brushes, hidden
face culling splits the remaining visible region into rectangular fragments
instead of emitting the covered span. This keeps stacked floors, pits, and
prefab-adjacent wall runs from producing duplicate coplanar render surfaces
while preserving the original source face identity. The
runtime descriptor's `compile_artifact_hash` is a deterministic hash of the
compiled mesh/chunk metadata and can be used by tests, tools, and future offline
cache invalidation. Tools can also call
`slayer3d_game_data_export_brush_world_compile_artifact_json()` to write an
inspection manifest using `schema: "slayer3d.brush_compile_artifact.v0"`. The
manifest records a deterministic source hash for authored brush inputs, the
compile policy, source brush totals, editor source-model metadata when present,
render mesh totals, visible source-face mappings, spatial chunk summaries, and
visibility-grid metadata. Source totals separate renderable brushes from
non-renderable structural brushes such as `sky`, so tools can verify that sky
seals are present for leak detection without being emitted into the opaque world
mesh. For editor-authored worlds, the `source.editor_source_model` object records
whether the runtime was compiled from source boxes, plus the source box count,
snap units, and source units. Artifact verification treats mismatched
source-model metadata as stale even when the source and compile hashes are
otherwise present, which gives editors and offline compilers a direct guard
against trusting runtime-only or stale compiled payloads. It is intentionally a
descriptor rather than a binary cache payload: use the source hash, policy,
source-model status, and compile artifact hash together to inspect, compare, and
invalidate compiled artifacts before adding cache storage/loading.
Native editor/offline compiler hosts can call
`slayer3d_game_data_save_brush_world_compile_artifact_file()` to atomically save
that manifest beside authored brush fragments, then use
`slayer3d_game_data_verify_brush_world_compile_artifact_json()` or
`slayer3d_game_data_verify_brush_world_compile_artifact_file()` to decide whether
a descriptor still matches the current authored source and compile policy. A
valid-but-stale descriptor is not a load failure: verification returns status
flags so tooling can ignore stale artifacts and let the runtime rebuild from
source. When binary cache payloads are added later, this status check should be
the gate before any cached mesh/collision data is trusted.
Tools that need a stable cache location can call
`slayer3d_game_data_get_brush_world_compile_artifact_layout()` or
`slayer3d_game_data_save_brush_world_compile_artifact_layout()`. The canonical
layout is:

```text
<artifact-root>/brush/v0/<world-key>/<source-hash>/<compile-artifact-hash>/
  manifest.json
  render.payload.bin       (reserved)
  collision.payload.bin    (reserved)
  visibility.payload.bin   (reserved)
```

`<world-key>` is a filesystem-safe key derived from the brush world name with a
hash suffix to avoid collisions. `<source-hash>` changes when authored brush
source changes; `<compile-artifact-hash>` changes when the compile policy or the
compiled artifact metadata changes. The reserved payload names are intentional
contract boundaries for future binary mesh, collision, and visibility cache
files. Current runtime loading remains source-first and does not trust or load
those payload files.
When visibility culling leaves every brush in a compile chunk visible, the
renderer can draw that chunk as one optimized model instead of submitting each
brush model separately; partially visible chunks still fall back to per-brush
models so occlusion correctness is preserved. When
`acceleration` is enabled on a scene instance, active-scene trace queries use
those chunks and bounds as a broad phase before exact plane clipping.

Collision, weapon, sensor, and editor systems can query brush worlds through
the generic trace helpers. `slayer3d_game_data_trace_brush_world()` traces in a
named brush world's local space; `slayer3d_game_data_trace_active_brush_worlds()`
traces in active-scene world space and honors each `world.brush_worlds`
instance's placement. Traces support point/ray, swept sphere, and swept AABB
shapes, filter against `contents` masks, ignore `nocollide` faces, and report
the hit fraction, plane normal, brush, material, contents, and surface flags.
`slayer3d_game_data_slide_brush_world()` and
`slayer3d_game_data_slide_active_brush_worlds()` layer deterministic
plane-sliding over those traces for controllers and projectile movement.
`slayer3d_game_data_get_brush_diagnostics()` exposes cumulative trace counters
and brush render-counter deltas for debug UI, tests, and future editor
instrumentation; reset them with
`slayer3d_game_data_reset_brush_diagnostics()`. Brush render diagnostics are
derived from the generic render-context stats exposed by
`slayer3d_get_render_stats()`, because brush worlds compile to static model
meshes that use the same frustum culling path as other models.
When `visibility_occlusion` is enabled and a camera is available, brush bounds
are also checked against the active frustum before model submission. This
fail-open coarse pass rejects off-camera brush submodels before draw calls are
issued and reports `brush.frustum_brush_candidates`,
`brush.frustum_brush_culled`, and `brush.frustum_triangles_culled`.
Compile-time brush diagnostics are also exposed as `brush.compile_face_count`,
`brush.compile_rendered_face_count`, `brush.compile_culled_face_count`, and
`brush.compile_triangle_count`, plus geometry-health metrics
`brush.compile_invalid_brush_count` and
`brush.compile_degenerate_face_count`, so profiling overlays can verify how much
hidden interior surface area was removed before runtime rendering and whether
authored brush volumes compiled cleanly. The compile pipeline also exposes `brush.compile_chunk_count`,
`brush.collision_chunk_count`, and `brush.collision_chunk_reject_count` so tests
and profiling overlays can confirm trace broad-phase chunking is active and
rejecting irrelevant spatial groups before exact brush collision. Chunk render
diagnostics `brush.render_chunk_draws` and
`brush.render_chunk_brushes_drawn` confirm when visibility rendering uses
compiled chunk models rather than per-brush models.
Visibility-grid diagnostics also expose cache hit/miss counters so scenes can
confirm automatic occlusion is reusing the visible-cell set while the camera
remains in the same coarse grid cell.

Editor metadata attached to brush worlds, materials, brushes, and faces is
loaded into the runtime descriptors returned by
`slayer3d_game_data_get_brush_world()`. Tooling can use this metadata for
stable selection ids, hierarchy grouping, palette filtering, snap hints,
prefab/archetype references, and face inspectors while still previewing the
same runtime brush mesh that the game renders.

Editor viewports should pick brush and sector geometry through
`slayer3d_game_data_pick_editor_world_model()`. This wraps the generic
world-model trace API and returns a stable selection record with world,
sector/brush, material, face indexes, hit point, hit normal, bounds, and
runtime-owned editor metadata pointers. Debug overlays should consume
`slayer3d_game_data_for_each_editor_debug_primitive()` or
`slayer3d_game_data_draw_editor_debug_primitives()` for world bounds, selected
bounds, trace rays, hit markers, face normals, source face outlines, and source
vertex handles. These helpers are
renderer-agnostic/data-driven editor substrate; authored gameplay JSON does
not need to contain one-off debug actors for selection visualization.

Scenes can opt into a data-authored editor overlay with `editor.selection` and
`editor.debug_overlay`. This is useful for editor dojos and future in-engine
tools that should run through the generic runner instead of a custom native
host. `editor.selection.trace` describes a pick ray; `outputs` publishes the
latest selection to scene state so UI inspectors can display it. The debug
overlay renders world bounds, selected bounds, the trace ray, hit marker, and
face normal through the normal 3D presentation path:

```json
{
  "editor": {
    "selection": {
      "mode": "click",
      "select_button": "LEFT",
      "clear_on_miss": true,
      "trace": {
        "source": "camera_screen",
        "camera": "camera.editor.viewport",
        "viewport": [1280, 720],
        "near": 0.05,
        "far": 100.0,
        "model_filter": "brush_worlds",
        "contents_mask": "solid",
        "work_plane": {
          "enabled": true,
          "normal": [0, 1, 0],
          "normal_key": "editor.work_plane.normal",
          "distance": 0.0,
          "distance_key": "editor.work_plane.distance"
        }
      },
      "outputs": {
        "hit_key": "editor.selection.hit",
        "world_key": "editor.selection.world",
        "element_key": "editor.selection.element",
        "material_key": "editor.selection.material",
        "face_stable_id_key": "editor.selection.face_stable_id",
        "face_rendered_key": "editor.selection.face_rendered",
        "compiled_face_index_key": "editor.selection.compiled_face_index",
        "compiled_mesh_index_key": "editor.selection.compiled_mesh_index",
        "compiled_first_vertex_key": "editor.selection.compiled_first_vertex",
        "compiled_vertex_count_key": "editor.selection.compiled_vertex_count",
        "compiled_triangle_count_key": "editor.selection.compiled_triangle_count",
        "face_index_key": "editor.selection.face_index"
      },
      "hover_outputs": {
        "hit_key": "editor.hover.hit",
        "element_key": "editor.hover.element"
      }
    },
    "debug_overlay": {
      "enabled": true,
      "hover_selection_if": {
        "type": "scene_state.compare",
        "key": "editor.view.mode",
        "op": "==",
        "value": "flyby_3d"
      },
      "flags": [
        "work_plane_grid",
        "world_bounds",
        "selection_bounds",
        "selection_face",
        "trace_ray",
        "face_normal",
        "hit_marker"
      ],
      "work_plane_grid_color": [80, 140, 170, 95],
      "work_plane_grid_size": 24.0,
      "work_plane_grid_spacing": 1.0
    }
  }
}
```

Selection traces default to `source: "world"`, where `start` and `end` are
authored world-space vec3 values. `source: "camera_screen"` derives the ray
from a camera and screen point using the latest live mouse position by default.
Tooling can override that point with `screen`, `screen_x`/`screen_y`, or
`screen_x_key`/`screen_y_key`; `screen` may be a vec2 or the string `"center"`
for crosshair-style 3D editor picking. Viewport dimensions can likewise be
authored with `viewport`, `viewport_width`/`viewport_height`, or scene-state
keys. This lets editor dojos and future editor hosts share the same
data-authored picking primitive.

For brush-world selections, optional `face_rendered_key` and `compiled_*_key`
outputs report whether the selected source face survived compile-time hidden
face culling and where its triangles live in the optimized render mesh. Culled
or non-brush selections publish `face_rendered=false`, `compiled_face_index=-1`,
mesh/vertex indexes of `-1`, and zero vertex/triangle counts. This lets editor
inspectors and offline tools keep source brushes as the durable editing model
without losing the mapping to compiled render output.

Selection traces may also author `work_plane` as a fallback placement plane.
When the ray misses world geometry, the editor intersects the same ray with the
plane described by `dot(normal, point) = distance` and publishes that as a hit
with `selection_type: "none"`. This is intended for blockout tools: a blank
scene can still place the first floor on the ground plane, while later clicks on
real brush faces keep returning normal brush-world selections.

`selection_face` draws the selected source brush face outline separately from
the full brush bounds. This is useful for source-brush editors because the
selected face remains visible in tooling even when the compiler culls that face
from the optimized runtime render mesh as an internal hidden surface. Override
`selection_face_color` when the default green outline does not fit the editor
theme.

`vertex_handles` draws reusable handle markers for each vertex of selected
source brushes while an editor is in a vertex-editing mode. The handles are
emitted from the source brush topology rather than compiled render triangles, so
they remain stable when the brush compiler removes hidden/internal faces.
Override `vertex_handle_color` when the default flashing yellow does not fit the
editor theme.

`editor.debug_overlay.hover_selection_if` can be used when the overlay should
draw the live hover trace instead of the last clicked active selection. Full
screen 3D flyby editors should usually enable this while flyby mode is active so
selection bounds, hit markers, and trace feedback stay pinned to the center
reticle.
`normal_key` and `distance_key` may point at scene-state values, allowing a
single editor scene to switch between top/front/side orthographic plotting
planes with data-authored `scene_state.set` and `camera.set` actions.
Editor-authored player starts participate in picking as `selection_type:
"editor_player_start"` and publish their marker name as `selection_element`.

Add `work_plane_grid` (or `grid`) to `editor.debug_overlay.flags` to visualize
the authored work plane with reusable debug lines. `work_plane_grid_size` is the
grid half-extent in world units, and `work_plane_grid_spacing` controls line
spacing. The grid uses the same `work_plane.normal` and `work_plane.distance`
as placement, so visual feedback and picking stay aligned.

Add `markers` (or `diagnostic_markers`) to `editor.debug_overlay.flags` to draw
scene-state-driven diagnostic markers. Each entry in
`editor.debug_overlay.markers` requires a `point_key` that resolves to a vec3 in
scene state and may include `name`, `color`, `size`, and `visible_if`. This is
intended for actionable editor diagnostics, such as showing the first
source-model leak point returned by `editor.brush_world.validate_enclosure`.

Editor scenes should keep authoring mode and brush defaults in scene state so
the host, UI, and save pipeline all see the same values. The editor shell dojo
uses these conventional keys:

- `editor.mode`: high-level behavior such as `brush`, `select`, or `texture`.
- `editor.brush.prefab`: active blockout prefab such as `floor`, `wall`, or
  `ceiling`.
- `editor.brush.grid_size`: placement/snap size in world units.
- `editor.brush.height`: default vertical size for wall-like brush editing.
- `editor.brush.elevation`: default placement elevation.
- `editor.brush.thickness`: slab thickness for floor/ceiling-like brushes.
- `editor.brush.material`: active material reference for future brush/face
  paint operations.

Early editor milestones may mirror `editor.brush.prefab` into legacy placement
keys such as `editor.tool.mode`; newer editor logic should prefer the explicit
mode and brush-setting keys above.

`scene_state.set` accepts boolean, numeric, string, and vec3-array values. Vec3
scene-state values are useful for editor work-plane normals, runtime placement
offsets, and other authored tool state that should not require Lua.

Selection mode defaults to `hover`, where `outputs` receives the current pick
every frame. In `mode: "click"`, `outputs` receives the pinned selection and
`hover_outputs` can publish the live hover independently. `select_button`
defaults to `LEFT`; clicking empty space clears the pinned selection unless
`clear_on_miss` is false. `on_select` may reference a signal to emit after a
click updates the active selection and placement preview. The signal receives
the same selection payload used by `editor.selection.run`, which lets authored
editor tools turn one pointer click into a project-specific operation such as
"place the active prefab" while keeping selection, placement preview, and
command commits general-purpose.

Editor selections can also drive generic logic actions:

```json
{
  "type": "editor.selection.run",
  "actions": [
    {
      "type": "scene_state.set",
      "key": "editor.last_action",
      "value": "inspect {selection_element} face {selection_face_stable_id}"
    }
  ],
  "else": [
    { "type": "scene_state.set", "key": "editor.last_action", "value": "nothing selected" }
  ]
}
```

`editor.selection.run` executes `actions` only when the active scene has a
pinned or current selection. It supplies payload fields such as
`selection_type`, `selection_world`, `selection_element`,
`selection_material`, `selection_face_index`, `selection_face_stable_id`,
`selection_point`, and `selection_normal`. `editor.selection.clear` clears the
active selection and republishes the scene's `outputs` as an empty selection.

`editor.selection.select_brush` selects a known brush by name without requiring
a pointer pick. This is intended for editor diagnostics and repair workflows:
validation actions can publish a source brush and face to scene state, then this
action can turn that diagnostic into the same active selection used by the
debug overlay, inspector, and delete/resize tools.

```json
{
  "type": "editor.selection.select_brush",
  "world": "brush.editor_shell.target",
  "element_stable_id_from_state": "editor.leak.candidate_stable",
  "face_from_state": "editor.leak.candidate_face",
  "message": "selected leak candidate",
  "invalid_message": "no leak candidate to select",
  "outputs": {
    "valid_key": "editor.leak.candidate_selected",
    "message_key": "editor.leak.candidate_select_message"
  }
}
```

Exactly one brush identity is required: `element`, `element_from_state`,
`element_stable_id`, or `element_stable_id_from_state`. Prefer stable ids for
diagnostics and repair commands, because source brush names may be edited while
stable ids continue to identify the same authored brush. `face`,
`face_from_state`, `face_stable_id`, and `face_stable_id_from_state` are
optional; at most one may be supplied. `face` and `face_from_state` accept the
canonical box face keys `px`, `nx`, `py`, `ny`, `pz`, and `nz`, while
`face_stable_id` selects a face by its source/editor stable id. When the target
cannot be resolved, the action clears the active selection, publishes
`valid_key: false` when configured, and reports `invalid_message`. Set
`additive: true` to add a brush to the current selection instead of replacing
it.

Vertex mode publishes hover and selection scene-state under
`editor.vertex.hover.*` and `editor.vertex.selection.*`. Hover data includes
`hit`, `brush`, `brush_stable_id`, `vertex`, `index`, `shared_count`, and
integer source coordinates `x`, `y`, and `z`. Left-clicking a vertex handle
selects the vertex group at that shared source position across the currently
selected brushes; Shift+left-click toggles that group. Use
`editor.vertex.selection.clear` to clear only vertex handles while preserving
the brush selection and current editor tool. While vertex mode is active,
arrow-key nudges and left-dragging selected handles move selected vertices on
the current editor grid. Ctrl/Alt plus vertical movement constrains edits to
the world Y axis. Edits are applied through the source-brush validation path, so
off-grid, zero-volume, or otherwise invalid convex-box edits are rejected before
runtime brush geometry is rebuilt. The source model also has a convex plane
rebuild path for edited vertex sets: simple sloped/wedge-like solids can be
validated as convex brushes, original face materials are preserved when the
rebuilt face still maps to a canonical source face, and edits that would discard
an input vertex or collapse the hull are rejected. The same source-model layer
also exposes topology operation previews for add, delete, merge/fuse, and
snap-to-grid vertex workflows. Snap operations round source coordinates to the
requested fixed source-unit grid, then run the same convex rebuild validation as
every other source-vertex edit. These operations produce validated convex
runtime-brush output or a clear diagnostic. The apply path commits the same
validated result back to
`editor_brush_sources` as a durable convex source brush, so save/reopen and
test-run compilation preserve topology edits.

`editor.vertex.snap_selected` snaps the currently selected vertex source
brushes, or the selected brushes when no vertex handles are selected. By
default it reads `editor.grid.size` and converts that world-unit grid into the
source model's fixed units; `snap_units` can be authored for deterministic tool
commands and tests. Optional output keys publish `valid`, `message`,
`source_count`, `changed_count`, and `snap_units` values.

`editor.vertex.delete_selected` deletes the selected vertex handles from their
source brushes through the same convex rebuild validation path. Invalid deletes,
such as edits that would collapse a brush or discard required hull vertices, are
reported as diagnostics and leave the source model unchanged. Optional output
keys publish `valid`, `message`, `source_count`, and `deleted_count` values.
The editor shell routes Delete/Backspace to this action while vertex mode has an
active vertex selection; brush deletion remains unchanged in select mode.

`editor.vertex.merge_selected_to_hover` fuses selected source vertices into the
currently hovered vertex, or into an authored `target_vertex_index` when a tool
command needs deterministic targeting. Only selected vertices from the same
source brush as the target are merged, and the resulting convex source brush is
validated before commit. Optional output keys publish `valid`, `message`, and
`merged_count` values. Vertex-mode dragging uses the same command path when a
selected vertex is dragged onto a different hovered vertex.

`editor.vertex.add_to_source` adds a source vertex to a selected or explicitly
addressed source brush, then rebuilds the brush through the same convex source
validation path. Tools may provide either `coord` as fixed source units or
`position` as meters; exactly one is required. Optional `world`, `brush`, and
`brush_stable_id` fields make the target deterministic, and optional output keys
publish `valid`, `message`, `vertex_count`, and `added_count` values.
In the editor shell's vertex tool, Shift+left-clicking a selected source brush
face away from an existing vertex handle runs the same add command with a
grid-snapped point one active-grid step along the face normal, then selects the
new handle.

`editor.vertex.validate_source` runs source-vertex diagnostics for a selected
source brush set, an explicit `brush` / `brush_stable_id`, or the whole editable
brush world when no source selection is active. It publishes
`editor.vertex.diagnostics.*`, including `valid`, `message`, `world`,
`brush_count`, `vertex_count`, `edge_count`, `face_count`,
`shared_vertex_count`, `off_snap_count`, `degenerate_count`, `concave_count`,
`non_finite_count`, `first_issue`, and `first_issue_stable_id`. Optional
outputs can mirror those values to tool-specific scene-state keys. This action
is intended for inspector panels, issue lists, and repair workflows that need
the same source-model validity check used by vertex edit commits.

`editor_player_starts` is a mergeable editor/runtime section for player spawn
markers. It is deliberately separate from `entities`: a start records where a
test-run should place an existing actor, while the actor remains defined by the
game or template data. Each entry has a unique `name`, a required `position`
vec3, and optional `scene`, `target`, `yaw`, and `pitch` fields.

```json
{
  "editor_player_starts": [
    {
      "name": "player_start.level_01",
      "scene": "scene.level_01",
      "target": "entity.player",
      "position": [2.0, 1.6, -4.0],
      "yaw": 1.5708,
      "pitch": 0.0
    }
  ]
}
```

`editor_brush_sources` is the editor-owned source model for structurally correct
brush editing. Editable fragments include it alongside `brush_worlds`; when a
matching source world is present, Slayer3D compiles source boxes and convex
source brushes into the runtime brush world during load/import. Coordinates are
fixed integer source units so editor decisions round-trip without accumulating
floating-point drift;
`meters_per_unit` converts those source units to runtime meters. The default is
millimeter precision (`meters_per_unit: 0.001`), but source-backed mutations and
exports preserve the authored scale. Runtime `brush_worlds` remain meter-based
compiled output for renderer and collision. Editable level fragments loaded by
the editor must include matching `editor_brush_sources`; older runtime-only
brush fragments are not migrated or treated as editable graybox source. Each source
world references a brush world and stores stable source brush IDs, prefab
metadata, material references, and either integer `min`/`max` coordinates for
`kind: "box"` sources or integer `vertices` for `kind: "convex"` sources.
Convex sources are used after topology edits add, remove, or merge vertices.
`material` is the default material for generated faces.
`face_materials` may override individual generated box faces with keys `px`,
`nx`, `py`, `ny`, `pz`, and `nz`; omitted faces inherit `material`.

Each source box must have a unique `stable_id` and a unique brush `name` within
its source world. When `name` is omitted, it defaults to `stable_id`. The
runtime derives stable face ids from the brush stable id and canonical box face
keys, for example `floor.spawn.001.face.px`. This keeps selection, painting,
diagnostics, and undo/redo addressable by source brush/face identity instead of
by transient compiled-surface indexes.

`snap_units` is optional and defaults to `1`. When present, every source-box
`min` and `max` coordinate must remain aligned to that source-unit increment.
For example, with `meters_per_unit: 0.001` and `snap_units: 100`, source edits
are constrained to a 0.1 meter structural grid. The editor can still use larger
placement grids, but malformed off-grid source coordinates are invalid source
data: validation, editable-fragment import, and source-backed mutation tools all
reject them.

For source-backed editor worlds, the runtime keeps `editor_brush_sources` as an
in-memory source model. Successful editor mutations synchronize that source
model before save/export, then reload compiles runtime brushes from the source
again. This makes source boxes the durable editing truth and keeps generated
`brush_worlds` as load-time/runtime derived data.
When an editable fragment contains both `brush_worlds[].brushes` and a matching
`editor_brush_sources` entry, the source model wins: imported runtime brushes
are discarded and rebuilt from source, so stale compiled output cannot affect
editor preview, collision, selection, save/open, or test-run behavior.

Source-backed editor mutations validate candidate source boxes before they are
committed. Exact snapped face, edge, or vertex contact is legal. Positive-volume
overlap between structural source boxes is allowed in the editable source model
and reported as a warning diagnostic; the brush compiler is responsible for
removing hidden/internal faces from runtime render output.
Structural checks consider solid, player/projectile clip, and sky source boxes
as map-sealing architecture. Non-structural content such as trigger-only boxes
may overlap structural brushes for gameplay volumes; those boxes do not report
source-model overlap/gap/contact defects and do not seal leaks.

Use `editor.brush_world.validate_source` or the native
`slayer3d_game_data_validate_editor_brush_source_model()` API to inspect source
boxes before save or test-run. The pass runs on fixed source coordinates and
reports off-snap coordinates, positive-volume overlaps, and tiny non-zero near
gaps between boxes that otherwise overlap on the other two axes. Exact face
contact is counted as structural adjacency. Off-snap source coordinates are
blocking source-model defects. Overlaps and near-gaps are warning diagnostics
while the editor foundation is still evolving. The same pass also verifies that
the compiled runtime brushes and visible compiled render faces still resolve
back to source brush and face identities; source identity mismatches are
blocking defects because selection, save/open, rendering, and diagnostics would
otherwise be reading different worlds.

Editable source validation requires a matching `editor_brush_sources` model.
Missing source is a hard error for editor workflows so save/open, test-run,
selection, diagnostics, and compiled output all use the same canonical source
identity.

Use `editor.brush_world.validate_enclosure` or
`slayer3d_game_data_validate_editor_brush_source_enclosure()` to run the first
source-backed leak diagnostic. This pass derives an exact interval grid from
the fixed source box coordinates, marks structural source boxes as solid, and
flood-fills empty cells from an `editor_player_starts` marker. If the flood
reaches the expanded outside boundary, the playable space leaks. The editor
shell runs both validation actions before entering playable test-run mode.
During the current graybox-editor milestone, leak findings are warning-level:
they are visible in the inspector and debug overlay, but they do not block F5.
The `leak_point_key` output can feed an `editor.debug_overlay.markers` entry so the editor shows the
first reachable outside-boundary cell directly in the 3D view. The candidate
outputs identify the nearest source-box face on the same leak boundary axis,
giving editor UI a second point to highlight while the user decides which
missing or malformed brush should be repaired.
Lowered pits and stacked room blockouts should be represented as non-overlapping
source brushes: surrounding floor slabs, explicit vertical side-wall brushes,
and a floor/sky/ceiling seal. The enclosure diagnostic treats that shape as
watertight when all sides are present and reports a leak when one side wall is
missing.

```json
{
  "editor_brush_sources": [
    {
      "world": "brush.level.blockout",
      "coordinate_system": "fixed_millimeters",
      "meters_per_unit": 0.001,
      "boxes": [
        {
          "stable_id": "floor.spawn.001",
          "name": "brush.level.blockout.box.1",
          "kind": "box",
          "prefab": "floor",
          "material": "mat.editor.floor",
          "face_materials": {
            "px": "mat.editor.trim"
          },
          "min": [0, -200, 0],
          "max": [8000, 0, 8000],
          "contents": ["solid"]
        }
      ]
    }
  ]
}
```

```json
{
  "type": "editor.brush_world.validate_source",
  "world": "brush.level.blockout",
  "near_gap_units": 1,
  "outputs": {
    "valid_key": "editor.source.valid",
    "message_key": "editor.source.message",
    "box_count_key": "editor.source.boxes",
    "snap_units_key": "editor.source.snap_units",
    "off_snap_count_key": "editor.source.off_snap",
    "overlap_count_key": "editor.source.overlaps",
    "near_gap_count_key": "editor.source.near_gaps",
    "face_contact_count_key": "editor.source.face_contacts",
    "edge_contact_count_key": "editor.source.edge_contacts",
    "vertex_contact_count_key": "editor.source.vertex_contacts",
    "partial_face_contact_count_key": "editor.source.partial_contacts",
    "runtime_brush_count_key": "editor.source.runtime_brushes",
    "runtime_source_mismatch_count_key": "editor.source.runtime_mismatches",
    "compiled_face_count_key": "editor.source.compiled_faces",
    "compiled_face_missing_source_count_key": "editor.source.compiled_missing_source",
    "compiled_face_unknown_source_count_key": "editor.source.compiled_unknown_source",
    "first_issue_kind_key": "editor.source.issue.kind",
    "first_issue_source_name_key": "editor.source.issue.source",
    "first_issue_source_stable_id_key": "editor.source.issue.source_id",
    "first_issue_related_source_name_key": "editor.source.issue.related_source",
    "first_issue_related_source_stable_id_key": "editor.source.issue.related_source_id",
    "first_issue_source_face_key": "editor.source.issue.face",
    "first_issue_runtime_brush_name_key": "editor.source.issue.runtime_brush",
    "first_issue_runtime_brush_index_key": "editor.source.issue.runtime_index",
    "first_issue_compiled_face_index_key": "editor.source.issue.compiled_face_index"
  }
}
```

The first-issue outputs are optional structured context for the human-readable
message. `first_issue_kind` uses stable categories such as `off_snap`, `overlap`,
`near_gap`, `runtime_source_mismatch`, `compiled_missing_source`, and
`compiled_unknown_source`; source/runtime/compiled fields are empty or `-1` when
they do not apply.

```json
{
  "type": "editor.brush_world.validate_enclosure",
  "world": "brush.level.blockout",
  "player_start": "player_start.level_01",
  "max_cells": 262144,
  "outputs": {
    "valid_key": "editor.leak.valid",
    "message_key": "editor.leak.message",
    "open_boundary_count_key": "editor.leak.open_boundaries",
    "visited_cell_count_key": "editor.leak.visited_cells",
    "leak_point_key": "editor.leak.point",
    "leak_axis_key": "editor.leak.axis",
    "leak_side_key": "editor.leak.side",
    "candidate_name_key": "editor.leak.candidate",
    "candidate_stable_id_key": "editor.leak.candidate_stable",
    "candidate_face_key": "editor.leak.candidate_face",
    "candidate_point_key": "editor.leak.candidate_point",
    "candidate_distance_key": "editor.leak.candidate_distance"
  }
}
```

The generic runner can apply a marker directly:

```sh
build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --player-start player_start.level_01
```

When the marker has a `scene`, that scene is used as the initial scene unless
`--scene` is also provided. Supplying both is allowed only when they agree. The
target actor's position, `yaw`, and `pitch` are applied before camera setup and
before the first scene-enter signal runs.

Use `editor.brush_world.create_box` to append a new axis-aligned convex box
brush to a brush world. For placement-preview commits in source-backed editor
worlds, the action only commits the canonical source candidate produced by the
live preview command; it does not recompute runtime bounds or use a second
runtime-overlap path. The source model is the truth: validation runs against the
source box, runtime brush geometry is compiled output, and the world is marked
dirty only after source insertion and rebuild succeed. Generic non-preview
creation can still append a direct box for tests and low-level tooling, but the
editor floor/wall/ceiling/sky workflow uses source-backed prefab recipes.

Created structural boxes receive stable editor ids, exact face/edge/vertex
contact with existing structural brushes is allowed, and positive-volume overlap
with an existing structural brush is accepted as editable source data. Preview
and commit messages report overlap as a warning, and the brush compiler removes
hidden/internal faces from runtime render output.
The editor shell dojo demonstrates the current blockout palette pattern: modal
palette signals write scene-state selection strings, and the shared commit
signal branches to source-backed `editor.brush_world.create_box` or
`editor.player_start.place` actions. That keeps the first editor workflow
data-authored while a dedicated editor frontend is still evolving.
`editor.brush_world.validate_source` reports exact face, edge, and vertex
contacts separately, so editor tools can distinguish legal snapped contact from
warning-level near gaps or positive-volume overlap.

`contents` is optional and accepts the same brush-content string or string array
as authored brush JSON. If omitted, created boxes default to `solid`. Sky
prefabs should author `contents` such as `["sky", "player_clip"]`: the source
model can use the brush as a structural leak seal, collision can still block the
player, and the renderer can treat the brush as a sky boundary instead of an
ordinary opaque wall.

For interactive placement, set `position_from` to `selection_point`. The action
then treats `min` and `max` as offsets from the current active editor selection
point. `position_offset` can move the prefab anchor before creation, and `snap`
rounds the anchor to a grid size in world units before applying the offsets.

```json
{
  "type": "editor.brush_world.create_box",
  "world": "brush.level.blockout",
  "material": "mat.stone_floor",
  "position_from": "selection_point",
  "snap": 0.5,
  "min": [-4.0, -0.25, -4.0],
  "max": [4.0, 0.0, 4.0],
  "outputs": {
    "valid_key": "editor.create.valid",
    "message_key": "editor.create.message",
    "brush_key": "editor.create.brush",
    "bounds_min_key": "editor.create.bounds_min",
    "bounds_max_key": "editor.create.bounds_max",
    "dirty_key": "editor.brush_world.dirty",
    "revision_key": "editor.brush_world.revision"
  }
}
```

Scenes can also author `editor.placement` to show a live placement preview while
the mouse hovers over a brush or work plane. `tool_key` names the scene-state
property that selects the active tool. `snap_key` can point at a runtime
scene-state float so UI or keyboard shortcuts can change grid size without
reloading data. Individual previews can author `snap` to use a fixed local snap
instead; use that for game objects that need tighter placement than wall and
floor tiles. `grid_size_key` can point at the same scene-state float when box
previews use `grid_min` and `grid_max` instead of fixed `min` and `max` bounds.
Those grid bounds are multipliers from the snapped grid anchor, so a floor
authored from `[0, 0, 0]` to `[1, 0.025, 1]` becomes one active grid cell wide
at any configured grid size. For tile-like blockout tools, author floors,
walls, and ceilings from grid-boundary anchors with matching horizontal
footprints instead of centering them around the snap point; this keeps adjacent
tiles aligned without gaps. `contents` can also be authored on box previews; it
is copied into `editor.brush_world.create_box` when committing with
`"position_from": "placement_preview"`. Each preview entry maps a tool `mode`
to either a `box` ghost or a `player_start` marker. Box previews can use
`axis_key` with `axis` `x` or `z` to rotate wall-like prefabs between
horizontal grid axes. A box preview must author exactly one bounds
source: fixed `min`/`max`, or grid-scaled `grid_min`/`grid_max`. For
source-backed brush worlds, the preview and commit both use the same source
prefab command. Preview runs it in dry-run mode and publishes the exact source
candidate bounds; commit applies that candidate. If preview succeeds and no
other edit changes the source model before commit, the commit succeeds with the
same geometry. Floor, wall, ceiling, and sky are recipes that emit snapped source
boxes; runtime brush geometry is generated only after the source candidate
passes validation. The preview reuses the editor debug overlay's
`command_preview` flag and color, so editor hosts do not need a second rendering
path.
`drag_create` optionally enables TrenchBroom-style brush creation in select and
Brush Tool modes: the editor dry-runs the same source-box command while the
pointer is dragged in empty space, then commits that exact command on release.
The dragged footprint is projected onto the active editor work plane. The
dominant work-plane normal becomes the extrusion axis, so a horizontal Y-up
work plane draws an X/Z footprint and extrudes along Y, while front/side
orthographic work planes can draw X/Y or Y/Z footprints and extrude along Z or
X. `extrusion_axis` may be authored as `x`, `y`, or `z` to force an axis, or
`extrusion_axis_key` may read the axis from scene state when a tool wants to
override the active work plane.
`grid_widget` can
describe a small authored toolbar dropdown for selecting the active snap size.
The widget writes both the general grid key and the brush grid key so previews,
drag creation, selected-brush dragging, and arrow-key nudging stay in sync.

```json
{
  "editor": {
    "placement": {
      "tool_key": "editor.tool.mode",
      "snap_key": "editor.grid.size",
      "grid_size_key": "editor.grid.size",
      "default_snap": 1.0,
      "default_grid_size": 1.0,
      "drag_create": {
        "mode": "drag_box",
        "kind": "box",
        "world": "brush.level.blockout",
        "prefab": "editor.box",
        "material": "mat.stone_floor",
        "contents": ["solid"]
      },
      "outputs": {
        "active_key": "editor.placement_preview.active",
        "mode_key": "editor.placement_preview.mode",
        "bounds_min_key": "editor.placement_preview.bounds_min",
        "bounds_max_key": "editor.placement_preview.bounds_max"
      },
      "previews": [
        {
          "mode": "floor",
          "kind": "box",
          "world": "brush.level.blockout",
          "material": "mat.stone_floor",
          "grid_min": [0.0, -0.025, 0.0],
          "grid_max": [1.0, 0.0, 1.0]
        },
        {
          "mode": "wall",
          "kind": "box",
          "world": "brush.level.blockout",
          "material": "mat.stone_wall",
          "axis_key": "editor.wall_axis",
          "axis": "z",
          "grid_min": [0.0, 0.0, -0.0125],
          "grid_max": [1.0, 1.0, 0.0125]
        },
        {
          "mode": "sky",
          "kind": "box",
          "world": "brush.level.blockout",
          "material": "mat.sky",
          "contents": ["sky", "player_clip"],
          "grid_min": [0.0, 1.0, 0.0],
          "grid_max": [1.0, 1.025, 1.0]
        },
        {
          "mode": "player_start",
          "kind": "player_start",
          "size": [0.5, 1.8, 0.5]
        }
      ]
    },
    "grid_widget": {
      "grid_key": "editor.grid.size",
      "brush_grid_key": "editor.brush.grid_size",
      "open_key": "editor.grid.menu.open",
      "button": { "x": 735, "y": 45, "w": 110, "h": 28 },
      "popup": { "x": 735, "y": 76, "w": 110, "h": 288 },
      "row_height": 24,
      "values": [0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0]
    }
  }
}
```

Scenes may pair placement previews with `editor.palette` metadata. The palette
is intentionally data-only: it gives editor hosts and authored sidebar UI a
stable list of selectable tools without hard-coding project-specific prefab
names in C. `selected_key` usually matches `editor.placement.tool_key`.
Each entry has a `mode`, `label`, optional `shortcut`, optional
`category`/`description`, and an optional `preview` reference. When omitted,
`preview` defaults to `mode`. Validation requires every palette preview to
match an authored placement preview, so stale sidebar entries fail at load time.
Editor scenes can present that metadata however they like. The editor shell dojo
currently uses authored UI and logic signals: `B` opens the Brushes palette,
arrow keys move a scene-state cursor, `Enter` writes `selected_key`, and `Esc`
closes the modal. The Game Objects palette uses the same modal input shape for
player-start placement, and the Material palette is reserved for the face
painting workflow.

```json
{
  "editor": {
    "palette": {
      "selected_key": "editor.tool.mode",
      "entries": [
        {
          "mode": "floor",
          "preview": "floor",
          "kind": "box",
          "label": "Floor",
          "shortcut": "1",
          "category": "blockout"
        },
        {
          "mode": "player_start",
          "kind": "player_start",
          "label": "Player",
          "shortcut": "4",
          "category": "things"
        }
      ]
    }
  }
}
```

`editor.brush_world.create_box` and `editor.player_start.place` may use
`"position_from": "placement_preview"` to commit exactly the active preview.
Set `preview_mode` on the action to fail closed if the active preview belongs
to a different tool. This keeps authored preview geometry and committed
geometry in one place.

Use `editor.player_start.place` to create or update one runtime player start.
The action can be bound to editor tools that place a marker at a clicked point,
or to direct UI controls that place a known target actor at an explicit
position. `position` is optional when there is an active editor selection or a
target actor; otherwise it is required. `apply_to_target` defaults to true and
updates the target actor's position/yaw/pitch immediately so test-run previews
match the saved marker.

```json
{
  "type": "editor.player_start.place",
  "name": "player_start.level_01",
  "scene": "scene.level_01",
  "target": "entity.player",
  "position": [2.0, 1.6, -4.0],
  "yaw": 1.5708,
  "pitch": 0.0,
  "outputs": {
    "valid_key": "editor.player_start.valid",
    "message_key": "editor.player_start.message",
    "player_start_key": "editor.player_start.name",
    "position_key": "editor.player_start.position",
    "dirty_key": "editor.player_start.dirty",
    "revision_key": "editor.player_start.revision"
  }
}
```

Native editor hosts can call
`slayer3d_game_data_export_player_starts_fragment_json()` to serialize the
current player-start collection as a `slayer3d.fragment.v0` document containing
`editor_player_starts`. File writing remains host-owned, matching brush-world
exports.

Editor player starts are also selectable editor objects. The debug overlay flag
`player_starts` draws them as green cylinder markers by default; override
`player_start_color`, `player_start_radius`, or `player_start_height` under
`editor.debug_overlay` when a project needs different marker styling.
`editor.player_start.delete` removes a marker by explicit `name` or by the
active editor selection with `"name_from_selection": true`, which lets
right-click delete share the same authored secondary-select path as brush
deletion.

Use `editor.player_start.apply` to move a stored player-start target actor back
to its marker. This is useful for in-editor playable previews: apply the marker,
then switch to the authored play/test scene with `scene.set`.

```json
{
  "type": "editor.player_start.apply",
  "name": "player_start.level_01",
  "outputs": {
    "valid_key": "editor.test_run.enter.valid",
    "message_key": "editor.test_run.enter.message",
    "target_key": "editor.test_run.enter.target"
  }
}
```

Use `editor.command.preview` to declare a non-mutating command intent against
the active selection. This is the safe scaffolding layer for editor tools:
commands can publish UI state and draw preview bounds without modifying the
authored world. Supported `command` values are `translate`, `paint`, `resize`,
`extrude`, and `delete`; supported `target` values are `selection`, `world`,
`element`, `face`, and `material`. Paint previews require a `material` string
naming a material in the selected brush world's material palette. Resize and
extrude previews currently target brush faces and use `distance` along the
selected face normal; positive values grow the brush outward and negative values
shrink it.

```json
{
  "type": "editor.command.preview",
  "command": "translate",
  "target": "element",
  "offset": [0.0, 0.35, 0.0],
  "message": "preview translate {selection_element}",
  "outputs": {
    "active_key": "editor.command_preview.active",
    "valid_key": "editor.command_preview.valid",
    "message_key": "editor.command_preview.message",
    "element_stable_id_key": "editor.command_preview.element_stable_id",
    "face_stable_id_key": "editor.command_preview.face_stable_id",
    "bounds_min_key": "editor.command_preview.bounds_min",
    "bounds_max_key": "editor.command_preview.bounds_max"
  }
}
```

`editor.command.clear_preview` clears that preview state and can publish the
same `outputs` keys with `active_key`/`valid_key` set to false.

Use `editor.command.commit`, `editor.command.undo`, and `editor.command.redo` to
record validated editor command transactions. A commit requires an active command
preview in the current scene. `translate` commands targeting a brush `element`
mutate the selected runtime brush by shifting its planes by the preview offset,
then rebuild brush bounds and the compiled render mesh. `resize` and `extrude`
commands targeting a brush `face` move that face plane by the preview distance,
then rebuild brush bounds and the compiled render mesh. `paint` commands
targeting a brush `face` replace that face's material and rebuild the compiled
render mesh. Undo and redo apply the inverse/forward mutation and move the
command-history cursor. Other command names are accepted as transaction records
so editor UI can be authored ahead of future mutation implementations.
Successful brush-world mutations mark the affected runtime brush world dirty and
increment its editor revision. Transaction outputs can publish `dirty_key`,
`revision_key`, `saved_revision_key`, and `source_path_key` alongside the
transaction payload so editor UI can show unsaved state without host-specific C.
For editor source models, command previews and committed transaction records
capture brush and face stable ids in addition to display names/indexes, so
undo/redo and repair workflows continue to target the source brush even when the
compiled/runtime brush representation is regenerated.

```json
{
  "type": "editor.command.commit",
  "message": "committed {editor_command} #{editor_transaction_id_text}",
  "invalid_message": "nothing to commit",
  "outputs": {
    "valid_key": "editor.transaction.valid",
    "event_key": "editor.transaction.event",
    "message_key": "editor.transaction.message",
    "transaction_id_key": "editor.transaction.id",
    "undo_count_key": "editor.transaction.undo_count",
    "redo_count_key": "editor.transaction.redo_count",
    "element_stable_id_key": "editor.transaction.element_stable_id",
    "face_stable_id_key": "editor.transaction.face_stable_id"
  },
  "actions": [
    {
      "type": "scene_state.set",
      "key": "editor.tool.last_action",
      "value": "commit {editor_command} #{editor_transaction_id_text}"
    }
  ],
  "else": [
    { "type": "scene_state.set", "key": "editor.tool.last_action", "value": "nothing to commit" }
  ]
}
```

Transaction payloads include `editor_transaction_valid`,
`editor_transaction_event`, `editor_transaction_id`,
`editor_transaction_id_text`, `editor_command`, `editor_command_target`, `editor_transaction_scene`,
`editor_transaction_world`, `editor_transaction_element`,
`editor_transaction_element_stable_id`,
`editor_transaction_material`, `editor_transaction_previous_material`,
`editor_transaction_face_stable_id`, `editor_transaction_face_index`, `editor_transaction_offset`,
`editor_transaction_undo_count`, `editor_transaction_redo_count`, and
`editor_transaction_bounds_min`/`editor_transaction_bounds_max`.

Use `editor.texture.scan` to populate an editor texture browser from a configured
filesystem directory. By default it reads `editor.asset_source.textures.path`
and `editor.asset_source.textures.relative`, falling back to `textures` relative
to the loaded game-data file. The scan publishes rows into a runtime collection
and mirrors the first visible page into `editor.texture.slot.N.*` scene-state
keys so authored UI can bind labels, thumbnails, and buttons without hard-coded
paths. Files matching existing brush material textures reuse those material
names; new files register `mat.project.texture.<slug>` materials on the target
brush world.

```json
{
  "type": "editor.texture.scan",
  "world": "brush.level.blockout",
  "collection": "editor.textures",
  "slot_count": 6,
  "outputs": {
    "count_key": "editor.texture.count",
    "registered_count_key": "editor.texture.registered_count",
    "status_key": "editor.texture.scan.status"
  }
}
```

Use `editor.texture.select_index` to choose a scanned texture row by index. The
action updates `editor.palette.material.cursor`, `editor.texture.material`, and
`editor.texture.selected_index`. Slot-backed UI image warmup state such as
`asset_warmup.ui_image.image.editor_shell.texture.slot_0.pending` prevents
selecting textures that are still loading or failed.

Use `editor.actor.scan` to populate the editor Actor browser from built-in
primitive actors plus a configured model directory. By default it reads
`editor.asset_source.models.path` and `editor.asset_source.models.relative`,
falling back to `models` relative to the loaded game-data file. The scan
publishes rows into a runtime collection and mirrors the first page into
`editor.actor.slot.N.*` scene-state keys so authored UI can bind labels,
selection buttons, and model previews. Supported model files currently include
`.obj`, `.gltf`, `.glb`, and `.fbx`.

```json
{
  "type": "editor.actor.scan",
  "collection": "editor.actors",
  "slot_count": 6,
  "outputs": {
    "count_key": "editor.actor.browser.count",
    "status_key": "editor.actor.scan.status"
  }
}
```

Rows contain generic placement metadata such as `id`, `label`, `group`, `mesh`,
`model`, `name_prefix`, `classname`, `role`, `sensor_profile`, `color_*`,
`scale_*`, `path`, and `relative_path`. Scanned model rows get stable model ids
such as `model.project.actor.alpha_guard`; runtime rendering resolves those ids
from the actor browser collection while placed map data can preserve the
project-relative `model_path` property.

Use `editor.actor.select_index` to choose a scanned actor row by index. It
updates `editor.actor.selected_index`, `editor.actor.selected`,
`editor.palette.game_object.cursor`, `editor.mode`, and `editor.tool.mode`. If a
model-backed row is still warming up or has failed, selection is held back and
`editor.tool.last_action` reports `actor model loading` or
`actor model unavailable`.

Use `editor.actor.place_selected` to place the currently selected actor row into
an editor scene. The action copies the row's generic defaults into the placed
actor, including mesh/model, display group, color/alpha, scale, class/role
properties, `actor_browser_id`, and project-relative `model_path` when present.
Position can come from `placement_preview` or `selection_point`.

Use `editor.property.set` and `editor.property.remove` to mutate arbitrary
designer-authored key/value pairs on editor objects. The first implementation
supports `target_type: "editor_actor"` with `target` or `target_from_state`, and
`target_type: "selection"` for the selected editor actor. Keys can come from
`key` or `key_from_state`; values can be scalar JSON, vec3/color arrays,
`value_from_state`, or `value_from_payload`. Selection publishing mirrors the
first property slots into `editor.property.slot.N.key`, `.type`, `.value`, and
`.available` for generic inspector UI.

```json
{
  "type": "editor.property.set",
  "target_type": "editor_actor",
  "target_from_state": "editor.selection.element",
  "key": "opens_when",
  "value": "all_enemies_dead"
}
```

Use `editor.connection.mark_source` and `editor.connection.add` to author
generic event links between editor actors. Connections are intentionally data
driven: a source endpoint has an entity and event, a target endpoint has an
entity and action, and optional `properties` can carry game-specific meaning.
Games can ignore, extend, or reinterpret this data when loading a Slayer3D map.

`editor.connection.mark_source` records the selected actor, or an explicit
`target`, into `editor.connection.source.entity` with an `event` such as
`enter`, `activate`, or any game-specific event name. `editor.connection.add`
can then connect that stored source to the selected actor or to an explicit
`to_entity`. The editor shell uses this for a simple `Source` then `Connect`
workflow in the inspector.

```json
{
  "type": "editor.connection.add",
  "from_entity_from_state": "editor.connection.source.entity",
  "from_event_from_state": "editor.connection.source.event",
  "to_entity": "actor.room1.door",
  "to_action": "open",
  "properties": { "condition": "player_inside" }
}
```

Use `editor.prefab.define` to create or update reusable editor prefabs, and
`editor.prefab.instantiate` to place linked actor instances from actor prefabs.
An actor prefab can provide `archetype`, `mesh`, `model`, `group`, transform
defaults, `color`, and arbitrary shared `properties`. Instances keep unique
actor ids and a stable `prefab` back-reference. `prefab_overrides` names
properties that should stay per-instance when the source prefab changes.

```json
{
  "type": "editor.prefab.define",
  "id": "prefab.guard",
  "label": "Guard",
  "category": "Enemies",
  "kind": "actor",
  "archetype": "actor.enemy.guard",
  "mesh": "capsule",
  "properties": { "health": 100, "faction": "enemy" }
}
```

```json
{
  "type": "editor.prefab.instantiate",
  "prefab": "prefab.guard",
  "name_prefix": "actor.guard",
  "position": [3.0, 0.5, 2.0],
  "properties": { "health": 150 },
  "prefab_overrides": { "health": true }
}
```

Use `editor.prefab.unlink_actor` when an instance should keep its current data
but stop receiving future source-prefab updates.

Use `editor.brush_world.export` to serialize the current runtime state of one
brush world as a canonical `slayer3d.fragment.v0` JSON document. The export
includes runtime editor mutations such as translated brush planes and painted
face materials. Future editor hosts can write this JSON to their own project
document or source-control workflow; data-authored dojos can publish it to scene
state for validation and inspection. Native editor hosts can also call
`slayer3d_game_data_save_brush_world_fragment_file()` to atomically write the
same fragment JSON to a chosen filesystem path; a successful save marks the
world clean and records the saved source path. Hosts that save through their own
project pipeline can call `slayer3d_game_data_mark_brush_world_saved()` after
their write commits. Authored save actions are available for controlled editor
shells, but they require explicit filesystem paths and never accept `asset://`
destinations.

Use `editor.map.export` when the editor should publish the standalone
`.slayermap.json` artifact. The action emits the public `slayer3d.map` format,
including top-level materials, brushes, player-start actors, editor actors,
generic prefabs, and connections for inspection or game loading. It also embeds
the lossless editable fragment under
`editor.editable_level_fragment`, so reopening the map restores source-backed
brush editing state exactly. Native editor hosts can call
`slayer3d_game_data_export_editable_level_map_json()` or
`slayer3d_game_data_save_editable_level_map_file()` for the same JSON. A
successful native save marks both the selected brush world and player-start
collection clean at their current revisions, and marks placed editor actors,
prefabs, and connections clean after their editable data is written. Map export requires a
source-backed brush world and runs source/compiled identity checks before
writing JSON, so stale runtime-only brush data cannot become the saved level.

```json
{
  "type": "editor.map.export",
  "world": "brush.level.blockout",
  "outputs": {
    "valid_key": "editor.export.valid",
    "message_key": "editor.export.message",
    "json_key": "editor.export.json",
    "size_key": "editor.export.size",
    "brush_revision_key": "editor.brush_world.revision",
    "player_start_revision_key": "editor.player_start.revision"
  }
}
```

Use `editor.map.save` for the same unified artifact when an authored editor
shell is allowed to write a filesystem path directly. `path` is a literal host
path; `path_from_state` reads the path from scene state so a host/editor UI can
choose the destination. Exactly one must be provided. The action writes
atomically, creates parent directories, and publishes the same brush/player-start
revision state as `editor.map.export`.

```json
{
  "type": "editor.map.save",
  "world": "brush.level.blockout",
  "path_from_state": "editor.save.path",
  "outputs": {
    "valid_key": "editor.save.valid",
    "message_key": "editor.save.message",
    "path_key": "editor.save.path",
    "size_key": "editor.save.size"
  }
}
```

Use `editor.map.load` to replace an editor runtime's authored starter level with
a `.slayermap.json` file from disk. This is intended for editor hosts that pass
`editor.input.path` at launch time. The map is validated through the public map
APIs, then `editor.editable_level_fragment` is imported into the selected world.
Runtime-only fragments remain rejected by the fragment importer. Import
validates source/compiled identity before replacing the live runtime world, and
`editor_player_starts` replaces the runtime player-start collection. Set
`optional: true` when the same editor scene should also support new/blank
sessions with no input file.

```json
{
  "type": "editor.map.load",
  "world": "brush.level.blockout",
  "path_from_state": "editor.input.path",
  "optional": true,
  "outputs": {
    "valid_key": "editor.load.valid",
    "message_key": "editor.load.message",
    "path_key": "editor.load.path"
  }
}
```

The lower-level `editor.level.export`, `editor.level.save`, and
`editor.level.load` actions still exist for tooling that deliberately wants the
embedded `slayer3d.fragment.v0` document instead of the standalone map
container.

Use `editor.test_run.prepare` to publish a game-agnostic handoff manifest for
launching the generic runner from an editor-selected scene or player start. The
action does not spawn a process and does not write files. It produces
`slayer3d.editor_test_run.v0` JSON containing the root data asset, resolved
scene, player start, target actor, and runner argument array excluding mount
flags. Editor hosts combine those arguments with their current `--root`,
`--pack`, embedded, or fused launch context. The generic runner can consume the
manifest directly with `--test-run-manifest <path-or-asset>`.
For UI copy/paste affordances, `editor.test_run.prepare` and
`editor.test_run.save_manifest` can also author `runner`, one mount option
(`root`, `pack`, or `embedded`), and optional `media`. When `command_key` is
present in `outputs`, the action publishes a quoted runner command string.
Authored editor shells should usually run `editor.map.save` immediately before
test-run preparation so the runner sees the latest blockout map rather than the
last manually saved revision. If the runtime contains source-backed brush
worlds, source/compiled identity must validate before the handoff manifest is
exported.

```json
{
  "type": "editor.test_run.prepare",
  "data_asset": "asset://game.game.json",
  "player_start": "player_start.level_01",
  "outputs": {
    "valid_key": "editor.test_run.valid",
    "message_key": "editor.test_run.message",
    "manifest_json_key": "editor.test_run.manifest_json",
    "scene_key": "editor.test_run.scene",
    "player_start_key": "editor.test_run.player_start",
    "command_key": "editor.test_run.command"
  }
}
```

Use `editor.test_run.save_manifest` when the editor shell should write the
handoff manifest directly for `slayer3d_runner --test-run-manifest`. It accepts
the same `data_asset`, `scene`, and `player_start` fields as
`editor.test_run.prepare`, plus exactly one of `path` or `path_from_state`.

```json
{
  "type": "editor.test_run.save_manifest",
  "data_asset": "asset://game.game.json",
  "player_start": "player_start.level_01",
  "path_from_state": "editor.test_run.path",
  "runner": "./build/debug/slayer3d_runner",
  "root": "path/to/game/data",
  "outputs": {
    "valid_key": "editor.test_run.saved",
    "message_key": "editor.test_run.save_message",
    "path_key": "editor.test_run.path",
    "command_key": "editor.test_run.command"
  }
}
```

Use `editor.brush_world.status` to publish the current dirty/source state of a
brush world without exporting its JSON. Outputs support `valid_key`,
`message_key`, `world_key`, `dirty_key`, `revision_key`, `saved_revision_key`,
and `source_path_key`.

```json
{
  "type": "editor.brush_world.status",
  "world": "brush.level.blockout",
  "outputs": {
    "dirty_key": "editor.brush_world.dirty",
    "revision_key": "editor.brush_world.revision",
    "saved_revision_key": "editor.brush_world.saved_revision",
    "source_path_key": "editor.brush_world.source_path"
  }
}
```

Supported `model_filter` values are `all`, `sector_levels`/`sector`, and
`brush_worlds`/`brush`. Supported debug flags are `all`, `world_bounds`,
`selection_bounds`, `trace_ray`, `face_normal`, `hit_marker`, and
`command_preview`, plus `work_plane_grid` when a selection work plane is
authored.

Use `controller.editor_camera` on an actor to drive an editor/free-flight
viewport camera without collision:

```json
{
  "name": "entity.editor.camera",
  "active": true,
  "transform": { "position": [-5.0, 3.2, 5.0] },
  "properties": {
    "yaw": { "type": "float", "value": 0.98 },
    "pitch": { "type": "float", "value": -0.3 }
  },
  "components": [
    {
      "type": "controller.editor_camera",
      "actions": {
        "forward": "action.editor.camera.forward",
        "back": "action.editor.camera.back",
        "left": "action.editor.camera.left",
        "right": "action.editor.camera.right",
        "up": "action.editor.camera.up",
        "down": "action.editor.camera.down",
        "look": "action.editor.camera.look",
        "fast": "action.editor.camera.fast",
        "pan_left": "action.editor.ortho.pan.left",
        "pan_right": "action.editor.ortho.pan.right",
        "pan_up": "action.editor.ortho.pan.up",
        "pan_down": "action.editor.ortho.pan.down",
        "zoom_in": "action.editor.ortho.zoom.in",
        "zoom_out": "action.editor.ortho.zoom.out",
        "zoom_wheel": "action.editor.ortho.zoom.wheel"
      },
      "mode_key": "editor.view.mode",
      "orthographic_size_key": "editor.ortho.size",
      "move_speed": 8.0,
      "fast_speed": 22.0,
      "orthographic_pan_speed": 0.85,
      "orthographic_zoom_speed": 1.75,
      "mouse_sensitivity": 0.002,
      "pitch_min": -1.45,
      "pitch_max": 1.45
    }
  ]
}
```

Pair it with an `fps` camera targeting the camera actor. Movement uses the
actor's yaw on the horizontal plane; `up` and `down` move along world Y. The
optional `look` action gates mouse-look, which is useful for editors that need
normal mouse clicking and right-button camera look in the same scene. If `look`
is omitted, mouse-look is active whenever `mouse_look` is true. The controller
writes `yaw`, `pitch`, and `camera_forward` by default; override those names
with `yaw_property`, `pitch_property`, and `forward_property`.

When `mode_key` points at scene state with `orthographic_top`,
`orthographic_front`, or `orthographic_side`, the same controller switches to
orthographic canvas controls instead of flyby movement. `pan_left/right/up/down`
move the camera actor in the active orthographic plane, while
`zoom_in/out/wheel` scales the scene-state float named by
`orthographic_size_key`. Optional `orthographic_controls_if` gates those pan and
zoom controls with a data condition. This is useful for editors that reuse
arrow keys inside a modal palette: the palette can consume selection movement
while the orthographic camera stays still. Override `flyby_mode`, `top_mode`,
`front_mode`, or `side_mode` if a project uses different authored mode names.

Editor scenes can pair one free-flight camera actor with multiple authored
cameras. A typical graybox editor uses an `fps` or perspective camera for 3D
inspection and orthographic cameras for top/front/side plotting. For a classic
multi-viewport editor, author `world_viewports` on the scene:

```json
{
  "world_viewports": [
    { "name": "perspective", "camera": "camera.editor.perspective", "rect": [0, 0, 640, 360] },
    { "name": "top", "camera": "camera.editor.top", "rect": [640, 0, 640, 360] },
    { "name": "front", "camera": "camera.editor.front", "rect": [0, 360, 640, 360] },
    { "name": "side", "camera": "camera.editor.side", "rect": [640, 360, 640, 360] }
  ]
}
```

Each rect is `[x, y, width, height]` in logical pixels. Optional `active_if`
conditions let the same scene switch between a four-viewport layout and a
single full-screen flyby camera. World viewports affect only 3D/world drawing;
UI remains rendered at the normal logical resolution.

Selection traces may also declare matching `viewports` entries. When the mouse
falls inside a viewport rect, the trace uses that viewport's camera, local
screen coordinates, dimensions, and optional viewport-specific `work_plane`.
This keeps floor/wall/ceiling placement correct in orthographic quadrants while
still allowing a perspective preview in the same scene. Traces that omit
`viewports` fall back to the trace camera or active camera. A viewport entry may
set `"screen": "center"` to trace from the viewport center instead of the live
mouse position, which is the expected shape for full-screen 3D editor flyby
placement.

Use `controller.fps_brush` on an actor to drive first-person movement through
the active scene's brush-world instances:

```json
{
  "name": "entity.player",
  "active": true,
  "transform": { "position": [0.0, 1.6, 8.0] },
  "properties": {
    "yaw": { "type": "float", "value": 0.0 },
    "pitch": { "type": "float", "value": 0.0 }
  },
  "components": [
    {
      "type": "controller.fps_brush",
      "brush_world": "brush.keep_01",
      "contents_mask": ["solid", "player_clip"],
      "actions": {
        "forward": "action.move.forward",
        "back": "action.move.back",
        "left": "action.move.left",
        "right": "action.move.right",
        "jump": "action.jump"
      },
      "move_speed": 7.0,
      "jump_velocity": 5.8,
      "gravity": 14.0,
      "player_height": 1.6,
      "player_radius": 0.35,
      "step_height": 0.65,
      "ceiling_clearance": 0.1,
      "walkable_normal_y": 0.7,
      "mouse_sensitivity": 0.002
    }
  ]
}
```

The actor transform is the player's eye position. The controller sweeps an AABB
body through `world.brush_worlds` in active-scene world space, slides along
brush planes, supports basic stair stepping, gravity, jumping, and mouse-look,
and writes the same base diagnostic properties as `controller.fps_sector`.
`brush_world` is validated as the intended collision world; runtime collision
uses all active brush-world instances so multi-part brush scenes compose
naturally. Missing `contents_mask` defaults to `["solid", "player_clip"]`.
`walkable_normal_y` controls floor classification; the default `0.7` treats
surfaces up to roughly 45 degrees as walkable.

Brush FPS controllers also publish brush-specific diagnostics for tuning and
debug UI:

- `brush_collision_kind` (`none`, `wall`, `floor`, `ceiling`, or `solid`)
- `brush_collision_normal`
- `brush_collision_brush`
- `brush_collision_material`
- `brush_collision_contents`
- `brush_collision_surface_flags`
- `brush_floor_normal`
- `brush_floor_brush`
- `brush_step_up`

Each diagnostic property name can be overridden with the corresponding
`*_property` field, for example `brush_collision_kind_property`.

## Sector Levels

`sector_levels` describe sector/portal worlds as data. This is the reusable
foundation for Doom-like indoor maps: materials, sector floor plans, heights,
sector metadata, and baked lights are authored in JSON and loaded into runtime
`slayer3d_level` variants.

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
          "wall_material": "wall_metal",
          "lighting": {
            "level": 176,
            "color": [1.0, 0.72, 0.45, 0.65]
          }
        },
        {
          "name": "blue_hallway",
          "points": [[10, 2], [18, 2], [18, 6], [10, 6]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "rock_floor",
          "ceil_material": "wall_metal",
          "wall_material": "wall_metal",
          "lighting": {
            "level": 96,
            "color": [0.25, 0.45, 1.0, 1.0]
          }
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
- optional `lighting` as an object with `level` integer in `[0, 255]` and
  optional `color` vec3/vec4 values in `[0, 1]`
- optional lights with `position` vec3, optional `color` vec3, non-negative
  `intensity`, and positive `range`

Sector `lighting` is a Doom/Build-style local brightness and tint term. Missing
`lighting` preserves the legacy sector look. When present, `level` defaults to
`255` and `color` defaults to white. `color[3]`, when supplied, is tint
influence rather than transparency: `0` applies brightness only, `1` fully
applies the RGB tint, and values between them blend between white and the
authored RGB. Sector-local lighting is baked cheaply into sector floor, ceiling,
wall, step, and portal surfaces. Lit actor render components inside the active
scene's sector levels also receive the same sector-local modulation for sprites,
3D models, and mesh primitives. Components with `"lighting": false` are left
unchanged. Batched pickup layers and particles currently keep their authored
colors; use per-actor primitives when per-sector tint correctness matters for
those effects. Sector-local lighting composes with authored baked/static lights
and dynamic scene lights; it does not affect UI.

At load time Slayer 3D builds three runtime variants per sector level:

- `lightmapped`: sector-local lighting plus baked static lights in a lightmap
  atlas
- `vertex_baked`: sector-local lighting plus baked static lights in vertex
  colors, without a lightmap atlas
- `unlit`: material color/texture modulated by sector-local lighting, without
  baked static lights

Tools and data logic can update a sector at runtime with `sector_lighting.set`:

```json
{
  "type": "sector_lighting.set",
  "sector_level": "sector.level_1",
  "sector": "red_hallway",
  "level": 128,
  "color": [1.0, 0.1, 0.05, 1.0]
}
```

Use either `sector` or `sector_index`. Runtime updates rebuild the affected
level's render variants atomically, so they are suitable for editor preview and
occasional scripted changes. Avoid changing many large sectors every frame;
prefer authored sector values plus dynamic lights for high-frequency lighting
effects.

Scenes render sector levels by declaring instances under `world.sector_levels`:

```json
{
  "schema": "slayer3d.scene.v0",
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
        "sector_lighting": true,
        "sector_lighting_key": "render_sector_lighting",
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
`sector_lighting` defaults to `true`; when disabled, the instance uses matching
runtime variants built without sector-local lighting and actor primitives in the
instance no longer receive sector-local color modulation. `portal_culling`
defaults to `true`; when enabled, generic presentation computes visibility from
the active camera before drawing the level. `variant_key`,
`sector_lighting_key`, and `portal_culling_key` are optional scene-state keys.
When present, they override `variant`, `sector_lighting`, and `portal_culling`
at runtime. This supports data-authored debug/profile controls without custom
host-side input code.

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

FPS controllers also expose reusable actions for trigger volumes and scripted
events:

```json
{
  "type": "controller.fps.launch",
  "target": "entity.player",
  "vertical_velocity": 15.0
}
```

`controller.fps.launch` applies an upward impulse to either an
`controller.fps_sector` or `controller.fps_brush` actor and requires a positive
`vertical_velocity`.

```json
{
  "type": "controller.fps.teleport",
  "target": "entity.player",
  "position": [72.0, 4.1, 63.0],
  "yaw": -1.5707964,
  "pitch": 0.0
}
```

```json
{
  "type": "controller.fps.push",
  "target": "entity.player",
  "velocity": [4.0, 0.0, 0.0]
}
```

`controller.fps.push` applies a data-authored displacement to an FPS controller,
scaled by frame `dt` by default. Use it with `sensor.volume` stay/overlap edges
for conveyors, wind volumes, water currents, and other environmental pushes.
Set `"scale_by_dt": false` when the authored vector is already a one-shot
displacement.

`controller.fps.teleport` moves the controller and actor to the given
eye-position. `yaw` and `pitch` are optional; omitted angles preserve the
current orientation. Both actions also support `target_from_payload` instead of
`target` when a sensor payload supplies the actor name. The older
`controller.fps_sector.launch` and `controller.fps_sector.teleport` names remain
accepted for existing authored content.

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

Scene files use `slayer3d.scene.v0`. They can select entities, input actions,
menus, UI text, update phases, transitions, and activity hooks.

`scenes.initial` is the normal startup scene. Development tools may override it
at launch time; for example, `slayer3d_runner --scene scene.level_1` enters a
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

Scenes may also author `input.mouse_capture_if` with any normal data condition.
When present, the scene only captures relative mouse motion while that condition
is true. This is useful for editor shells that use normal hardware cursor
selection in orthographic views but switch to captured mouse-look in a 3D flyby
viewport.

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

`entities`, `actor_archetypes`, `actor_instances`, `actor_pools`, and brush
world objects may carry an optional `editor` object. The runtime ignores this
metadata during gameplay; it exists for editors, dojos, palette browsers,
prefab previews, and validation. Brush editor metadata is also exposed through
the loaded brush-world descriptors so tools can inspect worlds without
reparsing JSON.

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
- `stable_id`, `group`, `prefab`, and `archetype`: non-empty strings when
  present; brush-world stable ids are validated as unique inside each world
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
  "directional_offset": { "property": "camera_forward", "distance": 0.75 },
  "velocity_from_property": "camera_forward",
  "speed": 12.0,
  "cooldown_property": "fire_timer",
  "properties": { "owner": 1, "damage": 1 }
}
```

The target actor may define `fire_cooldown`; otherwise the action's `cooldown`
field is used. If the cooldown property is positive, no projectile is spawned.
Use `offset` for a literal world-space spawn offset. Use
`directional_offset` to offset along a normalized actor vec3 property, which is
the preferred FPS crosshair convention: the projectile starts on the same ray
it will travel. Use `velocity` for a literal projectile velocity, or
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
  "directional_offset": { "property": "camera_forward", "distance": 0.75 },
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
geometry through `sector_level`, brush geometry through `trace_brush_worlds`
and `brush_contents_mask`, select the nearest active actor with `target_tag`,
consume the same clip/ammo fields as projectiles, and run authored actions with
a payload:

```json
{
  "type": "weapon.hitscan",
  "target": "entity.player",
  "sector_level": "sector.e1m1",
  "trace_brush_worlds": true,
  "brush_contents_mask": ["solid", "projectile_clip"],
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
`origin`, `direction`, `hit_position`, `hit_distance`, `hit_actor`, `hit_wall`,
`hit_brush`, `hit_brush_world`, `hit_brush_name`, `hit_material`,
`hit_normal`, `hit_contents`, and `hit_surface_flags`. Use these fields for
damage, particles, lights, sounds, decals, or debug UI. Actor hit tests use a
target actor's `hit_radius` or `radius` property, falling back to the action's
`hit_radius` value. The sphere is centered on the actor position plus optional
`hit_center_offset`; use this for actors whose origin is at the feet rather than
the torso. Brush hits block actor hits behind the wall because the actor query
is limited to the nearest wall distance.

## Sector Level Fragments

Large sector worlds can be split by authoring concern without duplicating a
full `sector_levels` object in every file:

```json
{
  "schema": "slayer3d.fragment.v0",
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

`actor.spawn` and `actor.despawn` can also resolve actors from payload fields.
Use `position_from_payload` for payload Vec3 positions such as hitscan
`origin` or `hit_position`, and `payload_directional_offset` to push the spawn
point along a payload Vec3 such as `hit_normal`. `position_from_actor_properties`
builds a spawn position from three numeric properties on a source actor, with an
optional local `offset`. It can also add arrays of source properties through
`x_add`, `y_add`, and `z_add`. Use those additive fields for authored
weapon/effect anchors that need to follow a tunable viewmodel plus intro,
recoil, and bob offsets. `velocity_from_payload` reads a payload Vec3 direction,
normalizes it, multiplies by `speed`, and writes it to `velocity` or
`velocity_property` on the spawned actor. `properties_from_actor` copies
same-named properties from a source actor before applying the spawn action's
explicit `properties` object; use it for opt-in debug/tuning config actors that
should feed pooled effects without hard-coding the effect in C:

```json
{ "type": "actor.spawn", "pool": "pool.explosions", "from_payload": "other_actor_name" }
{ "type": "actor.spawn", "pool": "pool.tracers", "position_from_payload": "origin", "velocity_from_payload": "direction", "speed": 80.0 }
{ "type": "actor.spawn", "pool": "pool.decals", "position_from_payload": "hit_position", "payload_directional_offset": { "property": "hit_normal", "distance": 0.03 } }
{ "type": "actor.spawn", "pool": "pool.weapon_flash", "position_from_actor_properties": { "source": "entity.weapon_viewmodel", "x": "gun_x", "y": "gun_y", "z": "gun_z", "x_add": ["gun_intro_x", "gun_recoil_x", "gun_bob_x"], "y_add": ["gun_intro_y", "gun_recoil_y", "gun_bob_y"], "z_add": ["gun_intro_z", "gun_recoil_z", "gun_bob_z"] } }
{ "type": "actor.spawn", "pool": "pool.smoke", "position_from_payload": "origin", "properties_from_actor": { "source": "entity.weapon_smoke_config", "keys": ["smoke_size_scale", "smoke_alpha_scale"] } }
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
- `motion.brush_velocity_3d`: moves an actor by a `vec3` velocity property
  through active brush-world instances, using swept point, sphere, or AABB
  traces. It collides with `solid` and `projectile_clip` contents by default,
  can publish impact diagnostics, can run `impact_actions` or emit `on_impact`
  with brush/material payload data, and despawns on impact by default. Use this
  for projectiles, thrown props, and simple kinematic movers in true-3D brush
  worlds.
- `motion.patrol`: moves an actor through an authored list of 3D waypoints.
  Use `speed`, `wait_time`, `arrival_radius`, `mode` (`loop` or `ping_pong`),
  and optional `yaw_property` to expose movement direction to rendering or Lua
  logic. `animation_time_property` can advance model animation time while the
  actor walks. `yaw_forward` may be `"-z"`/`"negative_z"` (default) or
  `"+z"`/`"positive_z"` to match the rendered actor asset's authored forward
  axis, and `face_target` can orient the actor toward the next waypoint during
  turns. For true-3D brush worlds, add a `collision` object with
  `"type": "brush"` to move through active brush-world instances using swept
  point, sphere, or AABB traces, sliding, floor probing, and an optional
  `on_ground_property`. This is intended for simple NPC patrols and background
  actors that need to respect authored walls and floors without custom Lua.
- `lifecycle.ttl`: increments an age property every update and despawns pooled
  actors once the configured TTL is reached. Use this for short-lived bursts and
  effects instead of scanning active actors in Lua.
- `motion.grid_agent`: moves an actor along an authored `grid_maps` maze with
  queued turns, walkability checks, center snapping, and optional map wrapping.
- `controller.fps_sector`: first-person movement through a sector level using
  authored input actions, sector collision, jumping, gravity, stair stepping,
  and mouse-look.
- `controller.fps_brush`: first-person movement through active brush-world
  instances using authored input actions, AABB brush collision, jumping,
  gravity, stair stepping, and mouse-look.
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
- `viewmodel.bob`: writes additive first-person viewmodel offset/rotation
  properties by measuring movement of a source actor. Use it for weapon sway or
  walk bob without coupling the effect to a specific FPS controller. The
  component stores the source actor's previous position in
  `previous_position_property`, advances `phase_property` while the actor moves,
  and writes configured output properties such as `offset_y_property` and
  `roll_property`. `offset_amplitude`, `frequency`, `speed_scale`,
  `min_speed`, and `settle_rate` tune the feel.
- `light.point`, `light.spot`, and `light.directional`: actor-attached light
  components. They work on static actors and active pooled actors. Component
  lights inherit the actor transform and may use an `offset`. `enabled` defaults
  to `true`; `enabled_key` reads a scene-state boolean at draw time so debug
  menus and data-authored profile controls can disable a light group without
  removing actors.
- `particles.emitter`: actor-attached particle emitter. On pooled actors, the
  emitter is active only while the actor is active. `render_style` defaults to
  `default`; `soft_smoke` renders procedural, soft-edged GPU smoke billboards
  and `soft_fire` renders general flame billboards when a shader backend is
  available. `muzzle_flash` renders a short, irregular hot-core burst intended
  for firearm discharge effects. Shader styles fall back to the normal particle
  quad path in software. Effects can be tuned per actor with property indirection:
  `position_offset_property` reads a vec3 offset, `position_offset_x_property`,
  `position_offset_y_property`, and `position_offset_z_property` add scalar
  offsets, `size_start_property`/`size_end_property` override authored sizes,
  `size_scale_property` scales both sizes, `alpha_scale_property` scales color
  alpha, `emit_rate_property` overrides emission rate, and
  `emissive_intensity_property` overrides draw emissive intensity. `space`
  defaults to `world`; `space: "camera"` evaluates and draws particle positions
  in viewmodel camera space for weapon smoke, muzzle puffs, cockpit effects,
  and other first-person presentation effects that should stay attached to the
  viewport instead of lingering in world space. Camera-space particle positions
  use the same authoring convention as camera-space models: positive Z is
  forward from the camera and is converted to the renderer's view-space axis at
  draw time. Particle emitters also support `visible_to_cameras` and
  `hidden_from_cameras`, each authored as a camera name or array of camera
  names. Use those fields for first-person weapon effects, security camera
  overlays, mirrors, cutscene-only effects, and other camera-specific
  presentation.
- `render.cube`: renders a cube using authored `size`, or a vec3 actor property
  named by `size_property`. `texture` may reference an image asset id; each cube
  face is UV-mapped to the full image and tinted by `color`. The property path
  is useful for grid wall runs and other pooled actors that need per-instance
  dimensions.
- `render.mesh_primitive`: declares a procedural placeholder mesh descriptor.
  The canonical `primitive` values are `cube`, `sphere`, `capsule`,
  `cylinder`, `cone`, `torus`, `pyramid`, `wedge`, `plane`, `quad`, `disc`,
  `hemisphere`, `rounded_box`, `tube_segment`, `pipe`, `arrow`, and
  `billboard_plane`. `quad` is an alias for `plane`; `pipe` is an alias for
  `tube_segment`. `billboard_plane` is a flat 3D placeholder plane; use
  `render.sprite` when you need an actual texture-backed camera-facing
  billboard. These descriptors use the same transform, `color`, `texture`,
  `lighting`, `lighting_key`, and `emissive` fields as other render primitives
  where applicable, and add `draw_mode`: `solid` (default), `wire`, or
  `solid_wire`. `lighting_key` reads a scene-state boolean and is useful for
  isolating whether actor primitives, sector-local lighting, or dynamic lights
  are driving a scene. Set `"space": "camera"` for first-person procedural
  geometry such as weapon flashes, cockpit markers, and viewmodel-only debug
  shapes. Camera-space mesh primitive offsets use the same convention as
  camera-space models and particles: positive Z is forward from the camera, and
  the renderer converts it to view space at draw time. This is useful for
  authored first-person meshes and debug shapes; use `particles.emitter`
  `render_style: "muzzle_flash"` when a shader-shaped firearm burst is a better
  fit than solid geometry.
  Render primitive components also support camera-scoped visibility with
  `visible_to_cameras` and `hidden_from_cameras`, each authored as a camera name
  or array of camera names. This is useful for first-person bodies, security
  camera overlays, mirrors, cutscene-only props, and similar camera-specific
  presentation.
  `lod` defaults to `true` and lets the root `render.procedural_lod` settings
  reduce tessellation for distant procedural primitives. Set `lod` to `false`
  for inspection objects or silhouettes that must keep authored tessellation at
  any distance. `lod_bias` defaults to `1.0`; values above `1.0` make the
  primitive behave as if it is larger on screen and therefore keep higher detail
  longer, while values below `1.0` make it drop detail sooner. Camera-space
  primitives ignore procedural LOD.
  Dimension fields include `size`, `radius`, `height`, `radius_top`,
  `radius_bottom`, `major_radius`, `minor_radius`, `bevel_radius`, `arc_angle`,
  `segments`/`slices`, `rings`, and `tube_segments`. Authored property fields
  can override or scale common dimensions per actor:
  `size_property`, `radius_property`, `height_property`,
  `radius_top_property`, `radius_bottom_property`, and
  `size_scale_property`. `alpha_scale_property` scales the primitive color
  alpha, while `emissive_color`, `emissive_intensity`, and
  `emissive_intensity_property` provide data-tunable glow for effects. This is
  the stable data/runtime representation for procedural mesh primitives. The
  generic presentation path renders every primitive as shaded solid geometry by
  default, with optional wire-only or solid-plus-wire modes. Solid procedural
  primitives are generated through a presentation cache when hosts provide one,
  so repeated static placeholder geometry can reuse stable CPU mesh data and
  hardware backends can keep GPU buffers instead of rebuilding geometry every
  frame. Wire overlays are useful for inspection, but they still add extra line
  work and should be used selectively in large scenes.
- `render.composite`: expands one actor into multiple procedural
  `render.mesh_primitive` parts. Use `parts` as a non-empty array of mesh
  primitive descriptors. Part `offset` values are local offsets from the owning
  actor, and part fields such as `color`, `lighting`, `draw_mode`, dimensions,
  and rotation override the composite defaults. This is intended for quick
  proof-of-concept actors such as robots, ships, decorations, pickups, markers,
  and editor placeholders before bespoke model assets exist.
- `render.model`: renders an authored `assets.models` entry with `model`,
  optional `scale`, axis-angle rotation, `color` tint, and optional skeletal
  animation playback via `animation_clip` and `animation_time_property`.
  `animation_loop` defaults to `true` and wraps authored animation time by the
  selected clip duration. Set `"space": "camera"` for first-person viewmodels;
  in that mode `offset` and `offset_x_property`/`offset_y_property`/
  `offset_z_property` are interpreted as camera-local right/up/forward
  placement, and `rotation`, `pitch_property`, `yaw_property`,
  `roll_property`, and `scale_property` provide data-authored tuning controls.
  Additive placement and rotation fields (`offset_x_add_property`,
  `offset_y_add_property`, `offset_z_add_property`, `pitch_add_property`,
  `yaw_add_property`, `roll_add_property`) are evaluated on top of those base
  values. The plural forms (`offset_x_add_properties`,
  `offset_y_add_properties`, `offset_z_add_properties`,
  `pitch_add_properties`, `yaw_add_properties`, `roll_add_properties`) sum
  multiple additive properties, which lets authored effects such as recoil,
  pickup easing, and walk bob compose without mutating the base placement.
  `lod` defaults to `true` for world-space model props and lets root
  `render.model_lod_culling` skip tiny distant models. Set `lod` to `false` for
  enemies, important pickups, hero props, and other content that must remain
  visible regardless of projected size. `lod_bias` and `lod_cull_pixels` tune
  per-model behavior without host code.
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
ordered sets such as render profiles or debug variants. `cycle` accepts an
optional non-zero integer `direction`; use `-1` for previous-value shortcuts.

UI text bindings can read scene state with an optional scalar `default`, which
keeps HUD text renderable before the first action writes a transient value:

```json
{
  "format": "PROFILE %s",
  "bindings": [{ "type": "scene_state", "key": "render_profile", "default": "modern" }]
}
```

UI text can also bind engine metrics. `fps`, `frame`, and `paused` are always
available. Frame-performance metrics are sampled over
`presentation.metrics.fps_sample_seconds`, the same data-authored window used by
the FPS counter. They are intended for human-facing diagnostics and quick A/B
checks, not deterministic assertions. Available performance metrics are
`perf.frame_ms`, `perf.update_cpu_ms`, and `perf.render_cpu_ms`. The render CPU
time covers the managed data-game draw path and intentionally excludes backend
present/swap time. Renderer submission metrics are exposed as per-frame sampled
averages: `render.model_mesh_submissions_per_frame`,
`render.model_mesh_draws_per_frame`, `render.model_triangles_per_frame`,
`render.geometry_draw_calls_per_frame`,
`render.static_mesh_instanced_draw_calls_per_frame`,
`render.static_mesh_instances_batched_per_frame`,
`render.static_mesh_draw_calls_saved_per_frame`,
`render.procedural_lod_candidates_per_frame`,
`render.procedural_lod_reduced_per_frame`,
`render.procedural_lod_authored_triangles_per_frame`,
`render.procedural_lod_resolved_triangles_per_frame`,
`render.procedural_lod_triangles_saved_per_frame`,
`render.model_lod_candidates_per_frame`,
`render.model_lod_culled_per_frame`,
`render.model_lod_triangles_saved_per_frame`,
`render.depth_prepass_draws_per_frame`, and
`render.depth_prepass_triangles_per_frame`. The current world render target is
available as `render.world_scale`, `render.world_width`, and
`render.world_height`, which is useful when testing dynamic render scale while
keeping UI sharp. Per-object light selection exposes
`render.light_candidates_per_frame`, `render.lights_selected_per_frame`,
`render.light_selection_draws_per_frame`, and `render.light_selection_ratio` so
debug overlays can prove that lit draws are selecting a bounded subset of the
available scene lights. When `render.performance_queries` is enabled, capable
backends also expose `render.geometry_samples_per_frame` and
`render.depth_prepass_samples_per_frame`. These two metrics are useful for
depth-prepass A/B checks: in high-overdraw scenes, toggling pre-pass on should
lower main geometry depth-passing samples while adding a cheaper position-only
pre-pass sample count. Brush-world diagnostics are exposed with `brush.*` metric
names so debug overlays can inspect collision and render cost without
game-specific C:

```json
{
  "format": "RENDER %llu/%llu CULL %llu TRI %llu",
  "bindings": [
    { "type": "metric", "metric": "brush.render_mesh_draws" },
    { "type": "metric", "metric": "brush.render_mesh_submissions" },
    { "type": "metric", "metric": "brush.render_mesh_culled" },
    { "type": "metric", "metric": "brush.render_triangles_submitted" }
  ]
}
```

Supported brush metrics are `brush.trace_count`,
`brush.world_instance_count`, `brush.world_bounds_reject_count`,
`brush.brush_count`, `brush.contents_reject_count`,
`brush.bounds_reject_count`, `brush.collision_candidate_count`,
`brush.hit_count`, `brush.render_mesh_submissions`,
`brush.render_mesh_culled`, `brush.render_mesh_draws`, and
`brush.render_triangles_submitted`, compile counters
`brush.compile_face_count`, `brush.compile_rendered_face_count`,
`brush.compile_culled_face_count`, `brush.compile_triangle_count`,
`brush.compile_invalid_brush_count`, `brush.compile_degenerate_face_count`,
`brush.compile_chunk_count`, `brush.collision_chunk_count`, and
`brush.collision_chunk_reject_count`, chunk render counters
`brush.render_chunk_draws` and `brush.render_chunk_brushes_drawn`, frustum
counters `brush.frustum_brush_candidates`, `brush.frustum_brush_culled`, and
`brush.frustum_triangles_culled`, plus
visibility counters
`brush.visibility_brush_candidates`, `brush.visibility_brush_visible`,
`brush.visibility_brush_occluded`, `brush.visibility_triangles_culled`,
`brush.visibility_grid_cache_hits`, and
`brush.visibility_grid_cache_misses`.

For structured UI, prefer retained `ui.widgets`. Widgets are authored as a tree
and resolved to one flat render and hit-test stream each frame. This makes
layout, rendering, hit testing, dropdown popups, and dynamic text share one
source of truth. `ui.rects`, `ui.text`, and `ui.images` remain available for
small one-off overlays. `ui.panels` and `ui.inspectors` remain supported as
simple/legacy diagnostic helpers, but new editor shells, menus, and toolbars
should use `ui.widgets`.

```json
{
  "ui": {
    "widgets": [
      {
        "id": "ui.tools",
        "type": "toolbar",
        "layout": "row",
        "x": 0,
        "y": 0,
        "w": "fill",
        "h": 48,
        "padding": 8,
        "gap": 8,
        "color": [8, 12, 18, 235],
        "border_color": [88, 118, 160, 255],
        "border_thickness": 1,
        "children": [
          {
            "id": "ui.tools.select",
            "type": "button",
            "text": "Select",
            "font": "font.hud",
            "w": 120,
            "h": 32,
            "action": "signal.editor.mode.select",
            "text_color": [230, 236, 245, 255],
            "align": "center"
          },
          {
            "id": "ui.tools.grid",
            "type": "dropdown",
            "font": "font.hud",
            "w": 160,
            "h": 32,
            "options": ["Grid 1", "Grid 2", "Grid 4", "Grid 8"],
            "values": [1, 2, 4, 8],
            "open_key": "editor.grid.menu.open",
            "selected_value_key": "editor.grid.size"
          }
        ]
      },
      {
        "id": "ui.editor.fps",
        "type": "label",
        "font": "font.hud",
        "format": "FPS %s",
        "bindings": [{ "type": "metric", "metric": "fps", "default": 0 }],
        "x": 1100,
        "y": 16,
        "w": 160,
        "h": 28,
        "align": "right"
      }
    ]
  }
}
```

Widgets support `panel`, `toolbar`, `row`, `column`, `button`, `label`,
`dropdown`, `tab_strip`, `spacer`, `console`, and `log_view` nodes. `w` and `h`
may be positive numbers or `"fill"`. Nodes may author `color` / `fill_color`,
`border_color`, `border_thickness`, `text_color`, `text_scale`, and `align`
(`left`, `center`, or `right`). Dynamic text supports `format` plus `bindings`;
the number of `%s` placeholders must exactly match the number of bindings.
Single numeric scene-state labels can use `text_format` plus `text_value_key`.
Dropdown popup and option commands are synthesized from the authored dropdown
node and keep the dropdown `owner_id` for input and rendering. See
[`ui-widgets.md`](ui-widgets.md) for the retained UI authoring contract.

```json
{
  "assets": {
    "fonts": [
      { "id": "font.hud", "builtin": "Inter", "size": 22 },
      { "id": "font.pause", "builtin": "Inter", "size": 34 }
    ]
  },
  "ui": {
    "widgets": [
      {
        "id": "ui.hud.reticle",
        "type": "label",
        "font": "font.hud",
        "text": "+",
        "x": 630,
        "y": 338,
        "w": 20,
        "h": 44
      },
      {
        "id": "ui.hud.fps",
        "type": "label",
        "font": "font.hud",
        "format": "FPS %s",
        "bindings": [{ "type": "metric", "metric": "fps", "default": 0 }],
        "x": 1110,
        "y": 18,
        "w": 150,
        "h": 28
      },
      {
        "id": "ui.hud.resources",
        "type": "label",
        "font": "font.hud",
        "format": "HP %s/%s  Ammo %s/%s",
        "bindings": [
          { "type": "property", "entity": "entity.player", "key": "health", "default": 0 },
          { "type": "property", "entity": "entity.player", "key": "max_health", "default": 0 },
          { "type": "property", "entity": "entity.player", "key": "ammo", "default": 0 },
          { "type": "property", "entity": "entity.player", "key": "max_ammo", "default": 0 }
        ],
        "x": 24,
        "y": 668,
        "w": 480,
        "h": 28
      },
      {
        "id": "ui.pause.title",
        "type": "label",
        "font": "font.pause",
        "text": "PAUSED",
        "x": 530,
        "y": 300,
        "w": 220,
        "h": 44,
        "visible_if": { "type": "app.paused", "equals": true }
      }
    ]
  }
}
```

Retained widgets are authored in logical pixels. The presentation layer still
scales the final UI stream to the active logical/display resolution, so game
HUDs stay sharp while world rendering can use independent render scaling.

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

`branch` executes `then` actions when its `if` condition passes and `else`
actions when it does not. Either side may be omitted, which makes that branch a
successful no-op. Conditions normally read scene state or actor properties.
`scene_state.compare`, `property.compare`, and `payload.compare` support
`==`, `!=`, `<`, `<=`, `>`, and `>=` for numeric values, and `==` / `!=` for
boolean and string values. Actions that receive a runtime payload, such as
`weapon.hitscan` or sensors, can also use `payload.compare` with the same
comparison operators:

```json
{
  "type": "branch",
  "if": { "type": "scene_state.compare", "key": "debug.enabled", "op": "==", "value": true },
  "then": [
    { "type": "property.add", "target": "entity.debug_marker", "key": "x", "value": 1.0 }
  ]
}
```

```json
{
  "type": "branch",
  "if": { "type": "payload.compare", "key": "hit_wall", "op": "==", "value": true },
  "then": [
    { "type": "actor.spawn", "pool": "pool.impact_sparks", "position_from_payload": "hit_position" }
  ]
}
```

`debug.write_actor_properties` is a development-only action for explicit
placement and tuning workflows. It writes selected actor properties to a host
filesystem path when the action runs. Keep it behind opt-in tuning controls or
debug-only bindings; do not run it continuously during normal gameplay. Set
`append` to true to add another diagnostic block instead of replacing the file:

```json
{
  "type": "debug.write_actor_properties",
  "target": "entity.weapon.viewmodel",
  "path": "/tmp/gun-placement.txt",
  "append": false,
  "properties": ["gun_x", "gun_y", "gun_z", "gun_scale", "gun_pitch", "gun_yaw", "gun_roll"]
}
```

`property.animate` tweens a numeric, vec3, or color actor property over time.
It supports `target` or `target_from_payload`, `from`, `to`/`value`,
`duration`, `easing`, `repeat`, and optional `done_signal`. Omit `from` to
start from the property's current runtime value; this is useful for repeated
weapon recoil or other effects that may be re-triggered before the previous
animation has fully settled. Use `target_from_payload` for generic hit
reactions such as flashing whichever actor a hitscan weapon struck.

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

Weapon pickups are ordinary resource pickups. A firearm pickup can grant a
resource such as `revolver`, enable a camera-space `render.model` viewmodel with
`entity.set_active`, and use a `weapon.hitscan` action to spawn pooled muzzle,
smoke, tracer, and impact actors from the hitscan payload.

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

`sensor.brush_contents` detects whether an actor's current point is inside an
active brush matching an authored `contents_mask`. The default mask is
`trigger`, making it suitable for trigger volumes in brush worlds. It supports
`enter`, `stay` / `overlap`, and `exit`; payloads include `actor_name`,
`brush_world`, `brush_name`, `contents`, `surface_flags`, `hit_position`, and
`hit_normal`.

```json
{
  "name": "sensor.secret.trigger",
  "type": "sensor.brush_contents",
  "actor": "entity.player",
  "contents_mask": "trigger",
  "edge": "enter",
  "actions": [
    { "type": "property.set", "target_from_payload": "actor_name", "key": "found_secret", "value": true }
  ]
}
```

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

`sensor.brush_perception` is the brush-world equivalent. It uses active brush
world traces for line-of-sight instead of a sector level, and `contents_mask`
selects which brush contents block sight. The default blocker mask is
`["solid", "player_clip"]`. Payloads include the standard perception fields
plus brush trace fields such as `hit_brush`, `hit_brush_name`, `hit_material`,
`hit_contents`, and `hit_surface_flags`.

```json
{
  "name": "sensor.guard.brush_los",
  "type": "sensor.brush_perception",
  "observer": "entity.guard",
  "target_tag": "actor",
  "contents_mask": ["solid", "player_clip"],
  "range": 20.0,
  "fov_degrees": 120.0,
  "target_filter": { "relationship": "hostile" },
  "edge": "stay",
  "actions": [
    { "type": "property.set", "target": "entity.guard", "key": "alerted", "value": true },
    { "type": "property.set", "target_from_payload": "target_actor_name", "key": "was_seen", "value": true }
  ]
}
```

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
can call `slayer3d_game_data_validate_file()` directly. Runtime loading runs the
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
`treat_warnings_as_errors` in `slayer3d_game_data_validation_options` for stricter
authoring.
