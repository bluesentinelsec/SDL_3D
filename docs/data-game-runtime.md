# Data Game Runtime

`sdl3d_data_game_runtime` is the first engine-owned runtime layer for games
authored primarily with JSON, Lua, and assets. It is intended to replace
per-demo lifecycle code such as Pong's custom asset/data/caches/app-flow setup.

The runtime is game-agnostic. It does not know scene names, actor names, input
actions, or replication channels for a specific game.

## Responsibilities

The runtime currently owns:

- asset resolver creation
- caller-provided asset mounting
- root game JSON loading
- font, image, and particle presentation caches
- authored app-flow startup and frame update
- authored frame render through `sdl3d_game_data_draw_frame`
- haptics policy signal wiring
- input-profile hotplug refresh state
- ordered cleanup

The generic `sdl3d_runner` executable owns the outer SDL process loop for games
that only need the current data runtime surface. For now, Pong still keeps a
compatibility host for full LAN networking because network session
orchestration has not moved into the managed runtime yet.

## Startup Shape

A host creates a normal `sdl3d_game_session`, fills a
`sdl3d_data_game_runtime_desc`, and calls
`sdl3d_data_game_runtime_create`.

The descriptor requires:

- `session`: the game session that provides input, signals, timers, logic, and
  optional audio
- `data_asset_path`: the root game JSON asset path, for example
  `asset://pong.game.json`

The descriptor may also include:

- `media_dir`: built-in font/media root
- `mount_assets`: callback used to mount a directory, pack, or embedded pack
- `mount_userdata`: user data passed to `mount_assets`

## Frame Use

During the managed loop:

- call `sdl3d_data_game_runtime_update_frame` from tick and pause tick paths
- call `sdl3d_data_game_runtime_render` from the render callback
- call `sdl3d_data_game_runtime_refresh_input_profile_on_device_change` when a
  scene or host policy wants device-count hotplug rebinding

The outer loop still controls fixed timestep, SDL events, input snapshots,
audio frame updates, and presentation.

## Generic Runner

`sdl3d_runner` launches a game data asset without game-specific C callbacks.
It can mount either a development directory or a built `.sdl3dpak`, then reads
the app/window/audio config from the root game JSON before entering the managed
loop.

Development directory example:

```sh
build/debug/sdl3d_runner --root demos/pong/data --data asset://pong.game.json
```

Pack-file example:

```sh
build/debug/sdl3d_runner --pack build/debug/demos/pong/pong.sdl3dpak --data asset://pong.game.json
```

Optional flags:

- `--media <dir>` overrides the built-in media directory used for engine fonts
  and shared media.
- `--embedded` is available only when a build target defines
  `SDL3D_RUNNER_EMBEDDED_ASSETS` and provides the generic
  `sdl3d_runner_embedded_assets` pack blob symbols.

The runner is game-agnostic: it does not reference Pong symbols, scene names,
actions, actors, replication channels, or controls. Any remaining need for a
custom host indicates engine runtime work that should move into reusable JSON
or Lua-driven systems.

## Why This Matters

This API is a stepping stone toward fully data-only games. Once network session
orchestration is also managed by the engine runtime, a package such as Pong
should be able to ship as JSON, Lua, and assets rather than as a custom C
program linked against SDL3D.
