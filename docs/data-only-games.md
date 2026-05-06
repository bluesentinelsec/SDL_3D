# Data-Only Games

SDL3D's data-only target is that a game can ship as JSON, Lua, and assets
loaded by the generic engine runner. The game should not need a custom
`my_game/main.c` unless it is proving a new engine feature or integrating a
platform service the engine does not yet expose.

## Runtime Contract

The generic runner owns process-level behavior:

- SDL window and renderer creation from authored app/window config.
- Asset mounting from a development directory, `.sdl3dpak`, or embedded pack.
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

## Pong Status

Pong is now the reference data-only game:

- normal `pong_demo` builds from `tools/sdl3d_runner.c`
- Pong's embedded assets and `asset://pong.game.json` are build-time defaults
- Pong gameplay rules are authored in `demos/pong/data/scripts/pong.lua`
- scenes, options, input, audio, haptics, rendering, networking, diagnostics,
  and app flow are authored in JSON
- `demos/pong/main.c` remains only as an opt-in legacy compatibility host

Run Pong through the generic runner from a directory:

```sh
build/debug/sdl3d_runner --root demos/pong/data --data asset://pong.game.json
```

Run Pong through a pack file:

```sh
build/debug/sdl3d_runner --pack build/debug/demos/pong/pong.sdl3dpak --data asset://pong.game.json
```

Run the default embedded demo target:

```sh
build/debug/demos/pong/pong_demo
```

## Parity Checklist

Before deleting a legacy host, validate the runner-backed game path:

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
