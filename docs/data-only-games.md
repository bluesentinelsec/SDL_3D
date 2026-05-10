# Data-Only Games

Slayer 3D's data-only target is that a game can ship as JSON, Lua, and assets
loaded by the generic engine runner. The game should not need a custom
`my_game/main.c` unless it is proving a new engine feature or integrating a
platform service the engine does not yet expose.

## Runtime Contract

The generic runner owns process-level behavior:

- SDL window and renderer creation from authored app/window config.
- Asset mounting from a development directory, `.slayer3dpak`, embedded pack, or
  fused executable.
- Root game JSON loading.
- Fixed/update/pause/render callback ownership.
- Input snapshots, text input, and gamepad hotplug refresh.
- Audio frame updates and authored audio actions.
- Haptics policy wiring.
- Managed network session loops when the game opts in with authored data.
- Ordered shutdown.

The runner must stay game-agnostic. It may accept build-time defaults such as
an embedded asset symbol or root `asset://game.json` path, but it must not know
the game's scenes, actors, input actions, replication channels, control
messages, rules, scores, or UI labels.

## JSON Responsibilities

JSON is for reusable engine primitives and authored composition:

- app, window, renderer, storage, and audio defaults
- scenes, transitions, menus, widgets, text entry, and dynamic lists
- actors, components, cameras, lights, sprites, materials, and effects
- sector levels, FPS controllers, doors, platforms, hazards, skyboxes, and
  render-profile controls for first-person sector worlds
- input actions, bindings, profiles, and device-assignment policy
- sensors, timers, signal bindings, and generic logic actions
- options screens and settings reset/default behavior
- persistence declarations and authored save/cache paths
- network protocol, replication schemas, control messages, runtime bindings,
  session-flow events, keep-alive scenes, and diagnostics policy

If an author can express behavior by connecting existing primitives, it belongs
in JSON rather than host C.

## Lua Responsibilities

Lua is for game-exclusive rules or calculations that should not become engine
policy:

- scoring rules and win/lose conditions
- enemy or CPU decision logic
- serve/randomization rules
- game-specific collision responses or movement math
- level-specific scripts
- save-game interpretation and game-specific persistence payloads

Lua should call engine APIs through stable adapter contexts and keep reusable
systems reusable. If the same script pattern becomes common across multiple
games, promote the underlying primitive into JSON-backed engine behavior.

## When C Is Still Appropriate

C changes are appropriate when the game needs a new reusable engine capability:

- a new renderer/world model, such as tile maps, sectors, brush worlds, or open
  world streaming
- a new asset loader or compression/encryption primitive
- a new platform subsystem such as achievements, platform user identity, or
  store integration
- a new generic UI widget, input device feature, audio feature, or network
  transport feature
- a performance-critical reusable primitive that Lua cannot reasonably provide

Once implemented, the feature should be consumed from JSON and Lua by all games
that need it.

## Runner Examples

Run a data-authored game through the generic runner from a directory:

```sh
build/debug/slayer3d_runner --root path/to/game/data --data asset://game.game.json
```

Run through a pack file:

```sh
build/debug/slayer3d_runner --pack path/to/game.slayer3dpak --data asset://game.game.json
```

Bundle as a renamed single-file executable:

```sh
build/debug/slayer3d_bundle \
  --runner build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --output build/MyGame

build/MyGame
```

The bundled executable remains game-agnostic native code. The game-specific
content is the appended pack plus the footer's default data entry point.

Start directly in a scene while play-testing:

```sh
build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --scene scene.level_1 \
  --state checkpoint=midboss \
  --state lives=3
```

For larger setup payloads, use `--state-json '{"checkpoint":"midboss"}'` or
`--state-file dev/level_1_state.json`. Direct-start state is applied before the
scene enter signal runs, so authored setup logic can read it through normal
scene-state conditions and Lua state APIs.

Demo targets may compile the same runner source with embedded assets and a
default root data path. That wrapper should only supply build-time defaults; it
should not contain game rules.

## FPS Starter Template

`demos/templates/fps` is a project-agnostic starter for sector/FPS games. It is
designed to be copied into a new project rather than imported as a hidden engine
dependency. The template loads through the generic runner:

```sh
build/debug/slayer3d_runner \
  --root demos/templates/fps/data \
  --data asset://fps_template.game.json
```

The starter proves the data-only shape for common FPS primitives: WASD/mouse
input, an FPS sector controller, an FPS camera, a sector room,
navigation graph, reticle/FPS/resource HUD text, pause menu, projectile weapon,
projectile pool, pickup archetype, resource-station archetype, and editor
palette metadata. It uses colored geometry and built-in fonts so it remains
lightweight and game-agnostic.

## Mechanic Dojos

`demos/fps_mechanics_dojo` is the reference data-only mechanic testbed for
sector/FPS primitives. It can be launched normally or direct-started into a
focused scene:

```sh
build/debug/slayer3d_runner \
  --root demos/fps_mechanics_dojo/data \
  --data asset://fps_mechanics_dojo.game.json \
  --scene scene.dojo.hazards
```

Focused dojo scenes should use authored scene-enter setup to place the player
near the mechanic being tuned. That keeps the workflow equivalent to the final
runner path while avoiding splash/title/campaign flow during development.

`demos/mesh_primitives_dojo` is a graybox showcase for procedural 3D placeholder
geometry. It uses a sector-bounded room, one directional sun light, an FPS
camera, one differently colored actor for each `render.mesh_primitive` shape,
composite mock actors assembled from primitive parts, and spaced HUD labels plus
an FPS counter for quick performance checks:

```sh
build/debug/slayer3d_runner \
  --root demos/mesh_primitives_dojo/data \
  --data asset://mesh_primitives_dojo.game.json
```

`demos/lighting_dojo` is a data-only sector lighting showcase. It demonstrates
Doom/Build-style sector brightness, RGB tint influence, lit mesh primitives,
composite mock actors, and dynamic point lights in connected rooms:

```sh
build/debug/slayer3d_runner \
  --root demos/lighting_dojo/data \
  --data asset://lighting_dojo.game.json
```

`demos/brush_geometry_dojo` is a data-only brush-world rendering showcase. It
demonstrates true 3D convex brush slabs, ramps, overhangs, per-face materials,
and dynamic lighting through the same generic runner frame path:

```sh
build/debug/slayer3d_runner \
  --root demos/brush_geometry_dojo/data \
  --data asset://brush_geometry_dojo.game.json
```

## Data-Only Parity Checklist

Before treating a game as data-only, validate the runner-backed game path:

- startup loads the root game JSON and first scene
- splash/title/attract flow works
- single-player gameplay works
- local multiplayer input assignment works
- options menus, input rebinding, audio sliders, and display settings work
- pause/resume/title exit paths work
- LAN direct connect and LAN discovery work
- host/client enter the same network play scene
- network input, snapshots, pause, termination, and disconnect handling work
- audio and haptics policies still trigger where platform support exists
- pack-file and embedded asset launch paths both work

Automated tests should cover reusable primitives and headless multiplayer
logic. Manual parity passes are still useful for audio, haptics, and
cross-machine LAN behavior.
