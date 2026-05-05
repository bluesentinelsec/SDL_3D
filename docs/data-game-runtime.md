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

Hosts still own the outer SDL process loop through `sdl3d_run_game`. For now,
Pong also still owns network session orchestration. Future slices should move
that orchestration into a managed network runtime so Pong can run through a
generic runner without a custom `main.c`.

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

## Why This Matters

This API is a stepping stone toward a generic SDL3D runner. Once network
session orchestration is also managed by the engine runtime, a game package
should be able to ship as JSON, Lua, and assets rather than as a custom C
program linked against SDL3D.
