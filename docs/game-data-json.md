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
| `input` | no | Named actions and bindings. |
| `world` | yes | World identity, coordinate policy, bounds, cameras, lights. |
| `entities` | yes | Stable named actors with tags, transforms, properties, and components. |
| `signals` | no | Authored signal names. |
| `logic` | no | Sensors, timers, bindings, conditions, and actions. |
| `adapters` | no | Named game-specific extension points used by logic actions. |

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

Adapters are named extension points registered by game code. They should be narrow and inspectable.

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

Pong still keeps specialized math and policy in C adapters:

- `adapter.pong.serve_random`
- `adapter.pong.reflect_from_paddle`
- `adapter.pong.cpu_track_ball`

The JSON decides when those adapters run and handles the ordinary composition around them, such as score increments, round reset, serve timers, camera toggles, and goal/wall/contact sensors.

## Loader Acceptance Criteria

The first runtime loader should:

- parse `schema` and reject unsupported versions
- reject duplicate entity, signal, timer, sensor, and binding names
- validate references before gameplay starts
- validate component/action payload types
- preserve unknown properties under entity property bags
- allow unknown component types only when configured for permissive tooling mode
- expose precise error paths
- instantiate actor registry entries, signal names, timer definitions, and logic bindings without requiring sectors
