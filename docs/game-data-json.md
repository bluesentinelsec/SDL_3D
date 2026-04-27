# SDL3D Game Data JSON Format

This document defines the first JSON-family authoring format for SDL3D game data. It is intentionally small, explicit, and module-oriented so it can express Pong without assuming sectors, while still leaving room for Doom's FPS/sector modules.

The format is data-first: game rules should primarily be expressed as entities, components, sensors, signals, timers, conditions, and actions. Game-specific code should appear as named adapters only where a rule is not genuinely reusable.

## Format Goals

- Represent the generic model from `docs/game-data-model.md`.
- Keep sectors, tiles, rooms, and 3D scenes optional world modules.
- Prefer named references over runtime numeric ids.
- Let loaders validate missing targets, duplicate names, invalid action payloads, and unsupported module requirements before gameplay starts.
- Keep engine-owned primitives declarative and inspectable.
- Reserve game adapters for specialized math or policy, not ordinary wiring.

## Top-Level Object

Every file is a JSON object with these fields:

| Field | Required | Purpose |
| --- | --- | --- |
| `schema` | yes | Schema id. First value: `sdl3d.game.v0`. |
| `metadata` | yes | Human-readable identity and versioning. |
| `profiles` | no | Genre/profile hints and required modules. |
| `assets` | no | Stable asset ids mapped to paths or future archive refs. |
| `scripts` | no | Lua scripts that provide game-specific behavior used by adapters. |
| `input` | no | Named actions and bindings. |
| `world` | yes | World identity, coordinate policy, bounds, cameras, lights. |
| `entities` | yes | Stable named actors with tags, transforms, properties, and components. |
| `signals` | no | Authored signal names. |
| `logic` | no | Sensors, timers, bindings, conditions, and actions. |
| `adapters` | no | Named game-specific extension points used by logic actions or controllers. |

## Naming

Names are authored handles and should be stable across reloads. Use role namespaces:

- `entity.ball`
- `camera.main`
- `signal.score.player`
- `logic.goal.cpu`
- `timer.round.serve`
- `adapter.pong.reflect_ball`

Loaders should reject duplicate names inside a namespace and should report the JSON pointer of the failure.

## World

The world object declares one spatial context. It does not imply a sector map.

```json
{
  "name": "world.pong",
  "kind": "fixed_screen",
  "units": "world_units",
  "axes": { "horizontal": "x", "vertical": "y", "depth": "z" },
  "bounds": { "min": [-9.0, -5.0, -1.0], "max": [9.0, 5.0, 2.0] },
  "cameras": [],
  "lights": []
}
```

Future world kinds can include `tile_grid`, `room_graph`, `sector_map`, and `scene_3d`.

## Entities

Entities are the data form of actor registry entries plus optional component ownership.

```json
{
  "name": "entity.ball",
  "active": true,
  "tags": ["ball", "dynamic"],
  "transform": { "position": [0, 0, 0.12] },
  "properties": {
    "velocity": { "type": "vec2", "value": [0, 0] }
  },
  "components": [
    { "type": "render.sphere", "radius": 0.22 },
    { "type": "collision.circle", "radius": 0.22 }
  ]
}
```

Core fields are generic. Components are module-owned and may be ignored by runtimes that do not load that module.

## Logic

Logic is event composition:

- **Sensors** observe world/entity state and emit signals.
- **Timers** emit signals later or repeatedly.
- **Bindings** attach ordered actions to signals.
- **Conditions** gate actions.
- **Actions** mutate generic state, emit signals, start timers, or invoke adapters.

### Sensors

Example:

```json
{
  "name": "logic.goal.cpu",
  "type": "sensor.bounds_exit",
  "entity": "entity.ball",
  "axis": "x",
  "side": "min",
  "threshold": -9.22,
  "on_enter": "signal.goal.cpu"
}
```

Sensors should be reusable and non-sector unless their type explicitly names a sector module, such as `sensor.sector_enter`.

### Actions

Generic action types should cover common wiring:

- `property.set`
- `property.add`
- `property.toggle`
- `entity.set_active`
- `transform.set_position`
- `motion.set_velocity`
- `signal.emit`
- `timer.start`
- `timer.cancel`
- `camera.set_active`
- `effect.set_active`
- `audio.play_sound`
- `audio.crossfade_ambient`
- `adapter.invoke`

Actions run in array order. Conditions may be embedded per action.

```json
{
  "signal": "signal.score.player",
  "actions": [
    { "type": "property.add", "target": "entity.score.player", "key": "value", "value": 1 },
    { "type": "transform.set_position", "target": "entity.ball", "position": [0, 0, 0.12] },
    { "type": "timer.start", "timer": "timer.round.serve", "delay": 1.0, "signal": "signal.ball.serve" }
  ]
}
```

### Conditions

Conditions should be generic comparisons over properties, signal payloads, tags, and entity active state.

```json
{
  "if": { "type": "property.compare", "target": "entity.score.player", "key": "value", "op": ">=", "value": 10 },
  "then": [{ "type": "signal.emit", "signal": "signal.match.player_won" }]
}
```

## Adapters

Adapters are named extension points implemented either by native C callbacks registered at runtime or by Lua functions declared in JSON. They should be narrow and inspectable.

Lua-backed adapters are loaded from top-level scripts:

```json
"scripts": [
  {
    "id": "script.pong",
    "path": "scripts/pong.lua",
    "module": "pong.rules",
    "dependencies": [],
    "autoload": true
  }
],
"adapters": [
  {
    "name": "adapter.pong.reflect_from_paddle",
    "kind": "action",
    "script": "script.pong",
    "function": "reflect_from_paddle"
  }
]
```

The loader resolves script paths relative to the JSON file, validates script ids/modules/dependencies, loads modules in deterministic dependency order, and requires each Lua module file to return a table. Adapter functions are resolved from that module table at load time and stored as Lua registry references, so per-frame adapter calls do not perform dotted global string lookup.

Lua modules should avoid global namespace ownership. A typical module returns a table:

```lua
local rules = {}

function rules.reflect_from_paddle(ball, payload, ctx)
  local paddle = ctx:actor(payload.other_actor_name)
  if paddle == nil then
    return false
  end

  -- game-specific rule code
  ball.velocity = Vec3(7.0, 1.5, 0.0)
  return true
end

return rules
```

Lua adapters receive `(target, payload, ctx)`:

- `target` is an actor wrapper for the authored action/component target, or `nil`.
- `payload` is a table copied from the signal payload or component payload.
- `ctx` provides adapter context: `ctx.adapter`, `ctx.dt`, `ctx:actor(name)`, `ctx:random()`, and `ctx:log(message)`.

The preferred Lua API is intentionally game-script oriented. Scripts can check `sdl3d.api`, currently `sdl3d.lua.v1`, when they need to guard version-specific behavior:

```lua
local speed = ball:get_float("base_speed", 5.6)
ball.position = Vec3(0, 0, 0.12)
ball.velocity = Vec3.normalize(ball.velocity) * speed
```

Actor wrappers expose property helpers (`get_float`, `set_float`, `get_int`, `set_int`, `get_bool`, `set_bool`, `get_vec3`, `set_vec3`) plus `position` and `velocity` convenience fields. `Vec3` provides `new`, `length`, `normalize`, `clamp`, and arithmetic operators. `math.clamp` and `math.lerp` are also available. The lower-level `sdl3d.get_*` and `sdl3d.set_*` functions remain available as implementation primitives, but gameplay scripts should prefer the wrapper API.

Native C registration remains available for host applications that need engine-facing integrations or highly optimized behavior; re-registering an adapter name overrides the authored Lua binding.

Good Pong adapters:

- `adapter.pong.serve_random`: choose a constrained non-extreme ball serve vector.
- `adapter.pong.reflect_from_paddle`: apply Pong-specific deflection based on hit offset.
- `adapter.pong.cpu_track_ball`: move the CPU paddle toward the ball.

Bad Pong adapters:

- increment score
- start a timer
- emit a signal
- reset a transform
- toggle a camera

Those are generic actions and should remain data.

## Validation

Game data is validated before runtime state is instantiated. Host tools can call `sdl3d_game_data_validate_file()` directly to collect diagnostics without creating actors, input bindings, timers, or signal handlers. `sdl3d_game_data_load_file()` runs the same validation pass before loading scripts and wiring logic.

Diagnostics are designed for authored content. Errors include the source file, a best-effort JSON path, and the referenced name when a reference cannot be resolved. For example, a binding action that targets a missing entity reports the action path and the missing entity name. Validation currently checks:

- schema support
- duplicate names within entity, signal, script, adapter, input action, timer, camera, and sensor namespaces
- script ids, modules, dependencies, dependency cycles, and script file existence
- input binding structure
- sensor, timer, binding, action, component, adapter, and camera references
- supported generic logic action types and required action payloads
- warnings for suspicious data such as unused adapters, unused scripts, or unsupported component types

Validation warnings are non-fatal by default. Tooling can set `treat_warnings_as_errors` in `sdl3d_game_data_validation_options` when strict authoring is desired.

## Pong Data Proof

`demos/pong/data/pong.game.json` is the first concrete file using this format. It is loaded by the Pong demo through `sdl3d_game_data_load_file()`, which instantiates the authored actors, input actions, signals, timers, sensors, and bindings into the managed game session.

- fixed-screen non-sector world
- player and CPU paddles
- ball, walls, goals, scores, cameras, lights, particles
- input actions
- named signals
- bounds/contact sensors
- score/reset/serve/win bindings
- adapter calls only for serve randomness, paddle reflection, and CPU steering

Pong keeps specialized math and policy in Lua adapters loaded from `demos/pong/data/scripts/pong.lua`:

- `adapter.pong.serve_random`
- `adapter.pong.reflect_from_paddle`
- `adapter.pong.cpu_track_ball`

The JSON decides when those adapters run and handles the ordinary composition around them, such as score increments, round reset, serve timers, camera toggles, and goal/wall/contact sensors. The Pong executable does not register Pong-specific C rule callbacks; its C code owns presentation, rendering, and process lifecycle.

## Loader Acceptance Criteria

The first runtime loader should:

- parse `schema` and reject unsupported versions
- reject duplicate entity, signal, timer, sensor, and binding names
- validate references before gameplay starts
- validate script ids, modules, dependency order, and adapter function references before gameplay starts
- validate component/action payload types
- preserve unknown properties under entity property bags
- allow unknown component types only when configured for permissive tooling mode
- expose precise error paths
- instantiate actor registry entries, signal names, timer definitions, and logic bindings without requiring sectors
