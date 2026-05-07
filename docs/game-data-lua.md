# SDL3D Gameplay Lua API

SDL3D exposes a compact Lua API for game-specific rules that should remain data-driven and reloadable. The runtime owns the Lua state, loads script modules from authored game data, and injects a small helper surface that is stable across games.

The API version is currently `sdl3d.lua.v1`.

## Script Model

Lua scripts are authored as modules that return a table:

```lua
local pong = {}

function pong.reflect_from_paddle(ball, payload, ctx)
  -- rule code
  return true
end

return pong
```

When game data declares a script module:

- the loader resolves the script path relative to the JSON file
- the module must return a table
- module dependencies load in authored order before dependent adapters run
- module names may be published into the global Lua environment for debug access
- adapter functions are resolved once at load time and stored as Lua registry references

If a script is missing, returns a non-table, fails to load, or drops a referenced adapter function, the game data load fails before gameplay starts.

## Adapter Call Contract

Lua-backed adapters are invoked as:

```lua
target, payload, ctx
```

- `target` is an actor wrapper for the authored action/component target, or `nil` if the target could not be resolved.
- `payload` is a plain Lua table copied from the authored signal or component payload, or `nil` when the action has no payload.
- `ctx` is the runtime helper table for the current adapter call.

Adapters should return `true` when they accept the request and `false` when they reject it.

## Context Helpers

The `ctx` table provides:

- `ctx.adapter`: the adapter name being executed
- `ctx.name`: alias of `ctx.adapter`
- `ctx.dt`: delta time for this update
- `ctx:actor(name)`: resolve an actor wrapper by exact name; returns `nil` when missing
- `ctx:actor_with_tags(...)`: resolve the first actor matching one or more tags; returns `nil` when no actor matches
- `ctx:active_actors_with_tags(...)`: return an array of active actor wrappers matching one or more tags
- `ctx:spawn(pool, options)`: spawn one actor from an authored actor pool; returns `actor, actor_id, pool_index` or `nil, error`
- `ctx:despawn(actor_or_name)`: despawn a pooled actor or deactivate a static actor; returns `true` or `false`
- `ctx:despawn_by_tag(tag)`: despawn active pooled actors whose archetype has the tag; returns the despawn count
- `ctx:pool_capacity(pool)`: return the authored pool capacity, or `0` for an unknown pool
- `ctx:pool_active_count(pool)`: return the active actor count for a pool
- `ctx:pool_available_count(pool)`: return the inactive actor count for a pool
- `ctx:pool_peak_active_count(pool)`: return the highest active count reached since load
- `ctx:pool_spawn_attempt_count(pool)`: return attempted spawns
- `ctx:pool_spawn_success_count(pool)`: return successful spawns
- `ctx:pool_spawn_failure_count(pool)`: return failed spawns
- `ctx:pool_exhaustion_count(pool)`: return times the pool was exhausted
- `ctx:pool_reuse_count(pool)`: return times `reuse_oldest` replaced an actor
- `ctx:pool_despawn_count(pool)`: return pooled actor despawns, including reuse replacement
- `ctx:pool_last_spawn_failure_reason(pool)`: return the last spawn failure reason, or `none`
- `ctx:pool_last_despawn_reason(pool)`: return the last despawn reason, or `none`
- `ctx:state_get(key, fallback)`: read persistent scene state
- `ctx:state_set(key, value)`: write persistent scene state; passing `nil` removes the key
- `ctx:random()`: deterministic per-runtime pseudo-random value in `[0, 1)`
- `ctx:log(message)`: write a gameplay log line
- `ctx.storage`: safe storage access table with `read`, `write`, `exists`, `mkdir`, and `delete`

### Performance Notes

- `ctx:actor(name)` is the cheapest way to reach a known entity name.
- `ctx:actor_with_tags(...)` scans the actor registry and should be used for authored role discovery, not inner-loop per-frame work.
- `ctx:active_actors_with_tags(...)` scans static entities and actor pools. It is useful for gameplay rules over small groups, but hot paths with many actors should use narrower engine sensors or future collection helpers.
- `ctx.storage.*` performs filesystem I/O and should be used for saves, settings, caches, and similar infrequent tasks.
- `ctx:random()` is deterministic within a runtime session, which makes it safe for gameplay logic that should replay or sync consistently.

## Actor Wrapper

`sdl3d.actor(name)` returns a lightweight wrapper for the named actor. The wrapper does not own the actor and does not copy its state. It is just a convenient handle for property access.

The wrapper exposes:

- `name`
- `position`
- `velocity`

And helper methods:

- `get_position()` / `set_position(Vec3)`
- `get_float(key, fallback)`
- `set_float(key, value)`
- `get_int(key, fallback)`
- `set_int(key, value)`
- `get_bool(key, fallback)`
- `set_bool(key, value)`
- `get_string(key, fallback)`
- `set_string(key, value)`
- `get_vec3(key, fallback)`
- `set_vec3(key, value)`

Behavior notes:

- Missing actor names return `nil` from `sdl3d.actor(name)`.
- `get_position()` returns `nil` if the actor is missing.
- `get_vec3()` returns `nil` if the actor is missing.
- `active` and `is_active()` read the actor's current runtime active flag.
- `despawn()` is shorthand for `sdl3d.despawn(actor)`.
- Property getters fall back to authored defaults when the property is absent.
- `position` and `velocity` are convenience fields backed by the same property accessors.

Example:

```lua
local ball = ctx:actor("entity.ball")
if ball ~= nil then
  ball.position = Vec3(0, 0, 0.12)
  ball.velocity = Vec3.normalize(ball.velocity) * 5.6
end
```

## `sdl3d` Global Helpers

The runtime installs a small `sdl3d` table for low-level access and utility helpers:

- `sdl3d.api` - current API version string
- `sdl3d.actor(name)` - create an actor wrapper
- `sdl3d.get_position(actor)`
- `sdl3d.set_position(actor, x, y, z)`
- `sdl3d.get_float(actor, key, fallback)`
- `sdl3d.set_float(actor, key, value)`
- `sdl3d.get_int(actor, key, fallback)`
- `sdl3d.set_int(actor, key, value)`
- `sdl3d.get_bool(actor, key, fallback)`
- `sdl3d.set_bool(actor, key, value)`
- `sdl3d.get_string(actor, key, fallback)`
- `sdl3d.set_string(actor, key, value)`
- `sdl3d.get_vec3(actor, key)`
- `sdl3d.set_vec3(actor, key, x, y, z)`
- `sdl3d.dt()`
- `sdl3d.state_get(key, fallback)`
- `sdl3d.state_set(key, value)`
- `sdl3d.random()`
- `sdl3d.actor_with_tags(...)`
- `sdl3d.active_actors_with_tags(...)`
- `sdl3d.spawn(pool, options)`
- `sdl3d.despawn(actor_or_name)`
- `sdl3d.despawn_by_tag(tag)`
- `sdl3d.pool_capacity(pool)`
- `sdl3d.pool_active_count(pool)`
- `sdl3d.pool_available_count(pool)`
- `sdl3d.pool_peak_active_count(pool)`
- `sdl3d.pool_spawn_attempt_count(pool)`
- `sdl3d.pool_spawn_success_count(pool)`
- `sdl3d.pool_spawn_failure_count(pool)`
- `sdl3d.pool_exhaustion_count(pool)`
- `sdl3d.pool_reuse_count(pool)`
- `sdl3d.pool_despawn_count(pool)`
- `sdl3d.pool_last_spawn_failure_reason(pool)`
- `sdl3d.pool_last_despawn_reason(pool)`
- `sdl3d.log(message)`
- `sdl3d.storage.*`
- `sdl3d.json.*`

The low-level `sdl3d.get_*` and `sdl3d.set_*` helpers are useful for compact scripts and host integration. Gameplay scripts should generally prefer the `Actor` wrapper for readability.

## Actor Pools

Lua can allocate from JSON-authored `actor_pools` without native game code:

```lua
local projectile, actor_id, pool_index = ctx:spawn("pool.player_shots", {
  from = ctx:actor("entity.player"),
  offset = Vec3(0, 0.5, 0),
  properties = {
    damage = 2,
    owner = "player",
    velocity = Vec3(0, 11, 0)
  }
})

if projectile ~= nil then
  ctx:log("spawned " .. projectile.name)
end
```

`options` is optional. Supported keys:

- `position`: `Vec3`, `{x,y,z}`, or `{x, y, z}` table for an explicit spawn position
- `from`: actor wrapper or actor name whose current position should be copied
- `offset`: vector added after `position` or `from`
- `properties`: scalar or vector property overrides applied after archetype reset

Vector-like tables used by `position`, `offset`, or vector property overrides
must provide `x` and `y` fields, or array-style `[1]` and `[2]` fields. The `z`
component is optional and defaults to the current/fallback `z` value for that
operation, which keeps 2D gameplay scripts concise while still allowing 3D
values when needed.

`spawn` resets the selected pooled actor to its archetype, activates it, applies
the final position and property overrides, and returns the actor wrapper,
registry id, and pool slot index. When the pool is missing or exhausted, it
returns `nil, error_message`.

`ctx:active_actors_with_tags(...)` returns only currently active actors. Static
entities are matched by their authored tags; pooled actors are matched by their
archetype tags:

```lua
for _, shot in ipairs(ctx:active_actors_with_tags("player_shot")) do
  if shot.position.y > 6.0 then
    shot:despawn()
  end
end
```

`ctx:despawn(actor_or_name, reason)` resets pooled actors back to archetype
defaults and marks them inactive. For non-pooled actors, it only clears the
active flag. The optional reason is stored in the pool's last despawn
diagnostic. `ctx:despawn_by_tag(tag, reason)` applies to pooled actors and
returns the number of active actors it despawned.

Pool helpers report capacity, active count, available inactive slots, peak
active count, spawn attempts, spawn successes, spawn failures, exhaustion
events, reuse events, despawns, and the latest spawn/despawn reason. Use these
diagnostics while tuning projectile, enemy, pickup, and effect pools. An
exhausted pool logs an application warning; repeated warnings usually mean the
authored capacity or exhaustion policy does not match the game's intended worst
case.

During signal handling, sensor updates, rendering traversal, and network
snapshot application, pooled despawns are lifecycle-safe: the actor becomes
inactive immediately, but the archetype reset is deferred until the protected
section ends. Actor wrappers therefore remain safe to inspect for the rest of
the callback. Pooled actors expose `pool_lifecycle` as `inactive`, `spawning`,
`active`, or `despawning` for diagnostics.

## `Vec3`

`Vec3` is available both as `sdl3d.Vec3` and as a global `Vec3` constructor.

It supports:

- `Vec3.new(x, y, z)` and `Vec3(x, y, z)`
- `Vec3.length(v)`
- `Vec3.normalize(v)`
- `Vec3.clamp(v, lo, hi)`
- `+`, `-`, and `*` arithmetic operators

Engine Lua APIs that accept vector-like tables require `x/y` or array-style
`[1]/[2]` fields. The `z` or `[3]` field is optional and defaults to the API's
current/fallback `z` value.

## JSON Helpers

`sdl3d.json.decode(text)` parses JSON text and returns the equivalent Lua value. On failure it returns `nil, error_message`.

`sdl3d.json.encode(value)` converts Lua values to JSON text. On failure it returns `nil, error_message`.

Supported values:

- `nil`
- booleans
- numbers
- strings
- arrays of supported values
- string-keyed object tables of supported values

Known limitations:

- object keys must be strings
- cyclic tables are not supported
- deeply nested values are rejected after a fixed recursion limit

Performance note: JSON encode/decode allocates and validates data. Use it for saves, settings, and import/export flows, not every frame.

## Storage Helpers

`ctx.storage` exposes the same safe storage API as `sdl3d.storage`:

- `read(path)` -> `text` on success, or `nil, error` on failure
- `write(path, text)` -> `true` on success, or `false, error`
- `exists(path)` -> `bool` only; invalid paths and missing storage collapse to `false`
- `mkdir(path)` -> `true` on success, or `false, error`
- `delete(path)` -> `true` on success, or `false, error`

Paths must use `user://` or `cache://`. Absolute paths, `..`, empty segments, backslashes, and extra URI schemes are rejected.

## Common Error Conditions

- Missing actor name: `ctx:actor()` returns `nil`.
- Missing actor tags: `ctx:actor_with_tags()` returns `nil`.
- Missing or exhausted actor pool: `ctx:spawn()` returns `nil, error_message`.
- Missing despawn target: `ctx:despawn()` returns `false`.
- Missing state key: `ctx:state_get()` returns the authored fallback.
- Missing JSON data: `sdl3d.json.decode()` returns `nil, error`.
- Unsupported JSON value: `sdl3d.json.encode()` returns `nil, error`.
- Storage path violation: storage helpers return `false, error` or `nil, error` depending on the call.

## Recommended Usage

- Use exact actor names for deterministic gameplay rules when the name is stable.
- Use tags for authored role discovery during scene setup or adapter initialization.
- Use `ctx.storage` and `sdl3d.json` for persistent data, not transient gameplay state.
- Keep per-frame Lua small and declarative; move heavy math or broad searches into C helpers when a rule becomes hot.

## Pong Example

Pong’s script module shows the intended style:

- `serve_random(ball, _, ctx)` chooses a constrained serve direction
- `reflect_from_paddle(ball, payload, ctx)` handles paddle deflection and jitter
- `cpu_track_ball(paddle, payload, ctx)` handles single-player CPU steering
- persistence helpers use `ctx.storage` and `sdl3d.json` for score save files

That module is a reference for how to keep game rules readable without pushing the engine into game-specific code.
