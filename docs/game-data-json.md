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
