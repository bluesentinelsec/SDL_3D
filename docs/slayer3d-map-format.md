# Slayer3D Map Format

Slayer3D maps are authored level documents. They describe reusable level
content such as brush geometry, materials, actor instances, asset references,
generic properties, and object connections. They are intentionally separate
from full game data files: a map is level content that a game or tool can load
and interpret, not a complete game runtime configuration.

The initial format is JSON and should use the `.slayermap.json` extension. The
schema is also published at
[`docs/schemas/slayer3d-map.schema.json`](schemas/slayer3d-map.schema.json).

## Engine API

The public engine header [`include/slayer3d/map.h`](../include/slayer3d/map.h)
provides the initial map I/O surface:

- `slayer3d_map_validate_json()` and `slayer3d_map_validate_file()` validate
  map documents without keeping a loaded handle.
- `slayer3d_map_load_json()` and `slayer3d_map_load_file()` parse, validate,
  and return an opaque `slayer3d_map_document`.
- `slayer3d_map_to_json()` serializes a loaded document back to canonical
  pretty JSON.
- `slayer3d_map_write_file()` writes a loaded document to disk.
- Query helpers expose metadata, global map state, top-level object counts,
  typed views for materials/brushes/actors, and arbitrary property JSON while keeping
  project-specific data preserved inside the document.
- `slayer3d_map_init_lighting_build_options()` and
  `slayer3d_map_build_lighting_plan()` provide a shared lighting build/bake
  planning surface for editor GUI commands, CLI commands, and caller code. The
  initial plan reports dynamic/static/area light counts, runtime preview counts,
  bake counts, and configured budget overages.
- `slayer3d_map_build_lighting_artifact_manifest_json()` emits deterministic
  JSON metadata for planned static-light artifacts. It does not bake texels yet;
  it reserves the shared artifact contract that later GUI, CLI, and caller bake
  paths will fill with concrete lightmap or vertex-light payloads.

The loaded handle is intentionally JSON-preserving. String pointers returned by
typed read helpers are borrowed from the document and remain valid until
`slayer3d_map_destroy()`. Arbitrary property values can be serialized with the
`slayer3d_map_get_*_property_json()` helpers and must be released with
`slayer3d_map_free_string()`.

For the initial playable-map loop, `slayer3d_map_build_playable_scene_desc()`
derives a minimal runtime-facing summary from a loaded document. It counts
texture/model assets, materials, box brushes, and actors, then resolves the
actor/object whose arbitrary properties include `type = "player-character"`.
Games can use that descriptor as the first handoff point before materializing
their own renderer, physics, and gameplay objects.

`slayer3d_map_write_playable_game_files()` builds on that descriptor and writes
a minimal data-game package under a caller-provided directory. The package
contains `playable_map.game.json` plus `scenes/play.scene.json`, converts map
box brushes into a runtime brush world, and spawns the player through the
existing `controller.fps_brush` component. Authored directional, point, and spot
lights are bridged into runtime `world.lights`; area lights currently fall back
to point-light preview until the baked-lighting pipeline materializes their
soft/static contribution. `pulse` and `flicker` light animations are bridged to
runtime light effects for immediate playable feedback, and `rotate`/`sweep`
animations are bridged to runtime direction-rotation effects for directional and
spot-light previews. Run the generated package with:

```sh
slayer3d_runner --root path/to/generated-package --data asset://playable_map.game.json
```

The standard runner can also materialize a saved map to a temporary generated
package and launch it directly:

```sh
slayer3d_runner --map path/to/level.slayermap.json
```

This bridge is intentionally conservative: it proves the first playable loop
with brush collision, material colors, authored global lighting, and live
runtime lights, while leaving external texture/model asset copying, baked light
artifacts, and game-specific actor behavior to later project integrations.

## Example Game Data

[`demos/slayermap_example`](../demos/slayermap_example) contains a small
training-room map and native loader example. The map demonstrates the intended
game-facing data surface: textured graybox brushes, placed actors, reusable
prefabs, authored lights/effects, a skybox reference, and generic event/action
connections that a game runtime can interpret.

The same demo directory also includes `maps/lighting_showcase.slayermap.json`,
a compact fixture for the lighting milestone. It authors directional, point,
spot, rectangular area, and spherical area lights, plus flicker, rotating, and
orbiting fireball-style light animation metadata, so editor, CLI, and runner
work can be tested against one map as lighting support matures.

Build it with `SLAYER3D_BUILD_DEMOS=ON`, then run
`slayer3d_slayermap_example` to load the bundled map through the public
`slayer3d_map_*` API and print the authored object counts.

## Versioning

Every map has a stable format id and integer version:

```json
{
  "format": "slayer3d.map",
  "version": 1
}
```

Readers must reject unknown major versions. Writers should preserve unknown
top-level fields and unknown object fields when possible so project-specific
data survives editor round trips. Project-specific extensions should prefer
namespaced fields such as `x_my_game`.

## Runtime Data vs Editor Data

Runtime map data lives in the root document under fields such as `assets`,
`materials`, `brushes`, `actors`, `connections`, `global`, and `properties`.

Editor-only state must live under `editor`. Runtime consumers may ignore this
field. Examples include viewport layout, selected tool, collapsed inspector
groups, asset browser scroll state, and non-runtime authoring hints.

The current Slayer3D editor writes source-backed graybox state by embedding its
editable fragment at `editor.editable_level_fragment`. The root-level
`materials`, `brushes`, and `actors` fields are emitted for generic map
inspection and future runtime materialization, while the embedded fragment is the
lossless path used by the editor's open/save workflow today.

## Root Fields

- `format`: Required string. Must be `slayer3d.map`.
- `version`: Required integer. Version `1` is the current initial format.
- `metadata`: Optional object with `id`, `name`, `author`, and `description`.
- `units`: Optional string, either `meters` or `source_units`.
- `coordinate_system`: Optional string. The initial format uses `y_up`.
- `global`: Optional map-level lighting and presentation defaults. Missing
  fields expand to engine defaults through `slayer3d_map_get_global_state()`.
- `assets`: Optional asset catalogs for textures, models, sprites, skyboxes, and
  effect definitions.
- `materials`: Optional material definitions used by brushes and faces.
- `brushes`: Optional array of authored brush geometry.
- `actors`: Optional array of placed gameplay/editor actors.
- `connections`: Optional generic links between authored objects.
- `properties`: Optional arbitrary map-level key-value properties.
- `prefabs`: Optional reusable prefab definitions for linked brush, actor, or
  mixed editor instances.
- `lights`: Optional dynamic/runtime and baked/build-time light markers.
- `effects`: Optional particles, fog volumes, fire/smoke markers, and similar
  effect primitives.
- `skybox`: Optional map-level skybox selection.
- `editor`: Optional editor-only metadata.

## Global Map State

`global` stores caller-agnostic defaults that affect the whole map. It is the
shared contract used by the editor, command-line baking tools, and game callers.
The initial fields are intentionally small:

```json
{
  "global": {
    "ambient_light": [54, 56, 64, 255],
    "clear_color": [12, 14, 18, 255],
    "exposure": 1.0,
    "tonemap": "aces",
    "lighting_preview_quality": "balanced",
    "fog": {
      "enabled": false,
      "mode": "none",
      "color": [22, 24, 30, 255],
      "start": 8.0,
      "end": 64.0,
      "density": 0.0
    },
    "properties": {
      "time_of_day": "afternoon"
    }
  }
}
```

Colors use the same 0-255 RGBA convention as materials and actor colors. Runtime
bridges may convert these values into normalized renderer inputs. `tonemap` may
be `none`, `reinhard`, or `aces`. `lighting_preview_quality` may be
`performance`, `balanced`, or `quality`; editors can use it to trade preview
speed against fidelity. `global.properties` is reserved for project-specific
state such as weather, biome, audio zones, or game rules that the generic editor
does not understand.

The bundled editor exposes these defaults through the top-toolbar Global panel.
Current controls provide afternoon/night/interior presets plus exposure,
tonemap, preview-quality, and fog cycling. Saving a map writes those values into
the same `global` object consumed by the public map API and playable runner
export path.

## Asset References

Asset entries use stable ids and project-relative paths:

```json
{
  "assets": {
    "textures": [
      { "id": "tex.floor.concrete", "path": "textures/concrete_floor.png" }
    ],
    "models": [
      { "id": "model.enemy.capsule", "path": "models/enemy_capsule.glb" }
    ],
    "sprites": [],
    "skyboxes": [],
    "effects": []
  }
}
```

Object fields may reference either a declared asset id or a project-relative
path. Portable maps should prefer declared asset ids. Absolute paths and URI
strings are warnings because they are not portable between machines.

## Materials

Materials provide reusable brush appearance:

```json
{
  "materials": [
    {
      "id": "mat.floor",
      "texture": "tex.floor.concrete",
      "color": [180, 180, 180, 255],
      "tint": [255, 255, 255, 255],
      "properties": {
        "footstep": "concrete"
      }
    }
  ]
}
```

`color` is useful for untextured grayboxing. `tint` is an explicit texture
modulation value and should not be applied unless an editor/game workflow opts
into tinting. RGBA values use numeric channels in the `0..255` range.

## Brushes

Brushes are authored world geometry. Version 1 supports simple box geometry and
plane-defined convex brushes:

```json
{
  "brushes": [
    {
      "id": "brush.floor.1",
      "geometry": {
        "kind": "box",
        "min": [-4, 0, -4],
        "max": [4, 0.25, 4]
      },
      "material": "mat.floor",
      "color": [128, 128, 128, 255],
      "properties": {
        "graybox_role": "floor"
      }
    }
  ]
}
```

Face-specific material/color/tint overrides may be represented with `faces`.
The editor may use side labels such as `top`, `bottom`, `north`, `south`,
`east`, and `west` for box brushes, or normals for more general brush forms.

## Actors

Actors are generic placed entities. They may represent gameplay actors,
triggers, spawn points, pickups, markers, or project-specific concepts the
editor does not explicitly understand.

```json
{
  "actors": [
    {
      "id": "actor.enemy.1",
      "archetype": "enemy.grunt",
      "model": "model.enemy.capsule",
      "primitive": "capsule",
      "transform": {
        "position": [2, 0.25, 1],
        "rotation": [0, 90, 0],
        "scale": [1, 1, 1],
        "facing": [1, 0, 0]
      },
      "color": [255, 80, 80, 180],
      "display_mode": "solid",
      "properties": {
        "health": 100,
        "team": "enemy"
      }
    }
  ]
}
```

`display_mode` is optional and may be `solid` or `wireframe` for editor/game
preview of primitive actors. `properties` values may be strings, numbers,
booleans, arrays, objects, or null. This is the primary escape hatch for
emergent gameplay data.

## Lights

Lights are first-class map objects for editor previews, bake jobs, and runtime
callers. The canonical light `type` values are `directional`, `point`, `spot`,
`area_rect`, and `area_sphere`. `kind` may be `dynamic`, `baked`, `static`, or
`both`; game callers may treat `static` as baked/precomputed, but it is kept as
a user-facing authoring term.

```json
{
  "lights": [
    {
      "id": "light.sun.afternoon",
      "kind": "baked",
      "type": "directional",
      "direction": [-0.25, -1.0, 0.15],
      "color": [255, 238, 180, 255],
      "intensity": 0.8,
      "shadow_mode": "baked",
      "properties": {
        "preset": "afternoon"
      }
    },
    {
      "id": "light.torch.1",
      "kind": "dynamic",
      "type": "point",
      "transform": { "position": [2.0, 1.5, -1.0] },
      "color": [255, 148, 72, 255],
      "intensity": 1.6,
      "range": 6.0,
      "casts_shadow": true,
      "shadow_mode": "dynamic",
      "falloff": "inverse_square",
      "animation": {
        "enabled": true,
        "type": "flicker",
        "preset": "torch_fire",
        "rate_hz": 8.0,
        "amplitude": 0.25,
        "min_intensity": 1.1,
        "max_intensity": 1.8
      }
    },
    {
      "id": "light.ceiling.panel",
      "kind": "baked",
      "type": "area_rect",
      "transform": { "position": [0, 2.9, 0], "rotation": [90, 0, 0] },
      "width": 2.0,
      "height": 0.6,
      "color": [210, 230, 255, 255],
      "intensity": 2.0,
      "shadow_mode": "baked"
    }
  ]
}
```

Spot lights use `inner_angle_degrees` and `outer_angle_degrees`; the engine
validator rejects spot lights whose outer cone is smaller than the inner cone.
Area sphere lights require a positive `radius`; area rect lights require
positive `width` and `height`. `falloff` may be `inverse_square`, `linear`, or
`none`. `animation` is a reusable primitive layer for common dynamic lights such
as torch flicker, fluorescent flicker, warning alarms, rotating sirens,
projectile fireballs, muzzle flashes, and steady room lights. Generated playable
maps currently bridge `pulse`/`flicker` to runtime intensity modulation,
`rotate`/`sweep` to runtime direction rotation, and `orbit` to runtime
position-orbit movement. Games may ignore unknown animation properties or
reinterpret them for a custom renderer.

The bundled editor exposes light placement through Things > Lights. Current
entries match the canonical schema: Directional, Point, Spot, Area Rect, and
Area Sphere. Selecting a placed light shows a light-specific inspector with
kind, intensity, range, shadow mode, falloff, color presets, spot cone presets,
area-size presets, and animation presets for steady, torch flicker, rotating
siren, and orbiting fireball behavior. The animation presets write the same
canonical `animation` object that hand-authored SlayerMap files use. The generic
Data inspector remains available for arbitrary key/value gameplay properties.

File > Plan Lighting and the Global Lighting panel's Plan Lighting button both
use `slayer3d_map_build_lighting_plan()`. This is the same public API intended
for editor GUI commands, editor CLI commands such as
`slayer3d_editor lighting-plan --input level.slayermap.json`, and caller code.
The planner reports total, dynamic, static, area, runtime-preview, and bake-light
counts, plus budget status. `slayer3d_editor lighting-plan --manifest --input
level.slayermap.json` emits the planned static-light artifact manifest JSON. It
does not bake lighting by itself; it is the shared front door for later
bake/compute implementations.

Generated playable-map packages also run the same lighting planner and emit a
small debug HUD into `scenes/play.scene.json`. The HUD shows light totals,
runtime and bake counts, class counts, and bake/budget status so
`slayer3d_runner --map` immediately exposes whether a saved map contains the
expected lighting work.

Map validation warns when authored lighting exceeds the default planning
budgets: 8 runtime-preview lights and 256 static/baked lights. These warnings
are advisory by default so editors can show the complete map state, but callers
may opt into `treat_warnings_as_errors` for stricter CI or export pipelines.

## Connections

Connections are optional generic links between authored objects:

```json
{
  "connections": [
    {
      "id": "connection.room1.complete_unlocks_door",
      "from": { "entity": "actor.room1.sensor", "event": "completed" },
      "to": { "entity": "brush.door.1", "action": "unlock" },
      "properties": {
        "condition": "all_enemies_dead"
      }
    }
  ]
}
```

Games may ignore, extend, or reinterpret connections. If an endpoint references
an object outside the map, set `external: true` on that endpoint.

## Prefabs

Prefabs are optional reusable definitions for preconfigured entities. Actor
prefabs may define `archetype`, `mesh`, `model`, `group`, transform defaults,
`color`, and arbitrary `properties`. Brush and mixed prefabs may carry `brushes`
and `actors` arrays using the same public object shapes as top-level map data.

Placed instances keep unique ids for per-instance gameplay logic. Actor
instances placed from a prefab write `prefab` and `prefab_linked`; linked
instances can receive shared updates from their source prefab while still
preserving explicit per-instance property overrides.

## Minimal Example

```json
{
  "format": "slayer3d.map",
  "version": 1,
  "metadata": {
    "id": "example.graybox",
    "name": "Graybox Example"
  },
  "units": "meters",
  "coordinate_system": "y_up",
  "assets": {
    "textures": [
      { "id": "tex.floor", "path": "textures/floor.png" }
    ]
  },
  "materials": [
    { "id": "mat.floor", "texture": "tex.floor" }
  ],
  "brushes": [
    {
      "id": "brush.floor",
      "geometry": { "kind": "box", "min": [-4, 0, -4], "max": [4, 0.25, 4] },
      "material": "mat.floor"
    }
  ],
  "actors": [
    {
      "id": "actor.player_start",
      "archetype": "player_start",
      "primitive": "capsule",
      "transform": { "position": [0, 0.25, 0], "facing": [0, 0, 1] }
    }
  ],
  "properties": {
    "music": "audio/level_theme.ogg"
  },
  "editor": {
    "camera": { "position": [6, 5, 6], "target": [0, 0, 0] }
  }
}
```
