# Pong C Host Audit

This audit records the state of Pong after the runner cutover. Pong's normal
demo target now builds from the generic SDL3D runner source with embedded Pong
assets and a default root data asset. The deprecated `demos/pong/main.c`
compatibility host remains available only through the opt-in
`SDL3D_BUILD_PONG_LEGACY_HOST` CMake option while parity is verified.

## Current Status

Pong gameplay is now substantially data driven:

- match rules, scoring, win/loss prompts, restarts, serve behavior, ball jitter,
  AI paddle movement, and presentation effects live in JSON/Lua
- options, title, pause, direct-connect, LAN discovery, lobby, and termination
  UI are authored scene/menu data
- input bindings and device-assignment policy are authored profiles
- host-to-client snapshots, client-to-host input, and control messages use the
  generic authored network replication schema
- direct-connect and LAN discovery forms/lists use generic text-entry,
  dynamic-list, runtime-collection, and network data actions
- no native UI context remains in `demos/pong/main.c`

The remaining Pong C is no longer part of the normal demo path and is not
gameplay-rule code. It is retained as a temporary compatibility host for
comparison and debugging.

## Remaining C Surface

`demos/pong/main.c` is still large because it preserves the old custom host.
The normal `pong_demo` executable no longer compiles it.

### SDL Process And Asset Bootstrap

The compatibility host still creates the SDL3D game config, mounts
embedded/pack/directory assets, loads `asset://pong.game.json`, initializes
caches, starts text input, and calls `sdl3d_game_run`.

This work has moved to `sdl3d_runner` and `sdl3d_data_game_runtime`. The
normal `pong_demo` target supplies only build-time defaults: embedded Pong
assets and `asset://pong.game.json`.

### Generic Data Game Loop

The first reusable runtime slice now lives in `sdl3d_data_game_runtime`. Pong no
longer owns asset resolver creation, game-data loading, app-flow/frame state,
presentation caches, haptics policy wiring, or authored frame rendering
directly.

`pong_tick`, `pong_pause_tick`, `pong_render`, and `pong_shutdown` still exist
only in the deprecated compatibility host. The generic runner now owns the
normal Pong loop through `sdl3d_data_game_runtime`.

- input profile hotplug refresh
- network session updates
- game-data frame update/render
- cleanup for remaining session state

Games opt into standard phases and policies from JSON instead of implementing
per-game callbacks.

### Managed Network Session Orchestration

Pong's legacy host still contains old multiplayer session orchestration, but
the generic data-game runtime now has an opt-in managed network path that
covers this responsibility for `sdl3d_runner` and the normal `pong_demo`:

- host session creation/destruction and lobby state publishing
- direct-connect session lifetime and status publishing
- per-frame UDP session updates
- packet receive/dispatch for control, input, and snapshots
- transition to the network play scene
- disconnect/timeout handling
- pause/resume synchronization
- authoritative host snapshot publishing
- client input publishing

Most packet content is already data driven. The managed runtime path is driven
by authored `network.session_flow`, `network.runtime_bindings`, and
`network.scene_state`, using standard semantics such as `host`,
`direct_connect`, `state_snapshot`, `client_input`, `start_game`, and
`disconnect`.

### Runtime State Publication

Runtime state publication for host lobby status, direct-connect status, match
termination messages, and network role/flow handoff now runs through authored
data actions and managed runtime bindings in the normal runner path.

### Diagnostic Logging

Snapshot diagnostic content and logging policy are now schema-driven. Pong C
only notifies the engine that a generic network snapshot event occurred; the
authored `network.diagnostics.snapshots` entry controls the channel, cadence,
log level, session-state inclusion, and message template.

## Remaining Pong-Specific C Literals

The remaining `PONG_*` constants in the compatibility host are mostly semantic
runtime binding names such as `state_snapshot`, `client_input`, `start_game`,
`pause_request`, and `direct_connect`. They are not hard-coded actor/property
packet schemas, and they no longer affect the normal `pong_demo` binary.

The generic runner now uses standard semantic binding names defined by the
engine. The remaining task is deleting the compatibility host when parity has
been verified.

## No Longer Blocking JSON/Lua Authorship

The following concerns have been removed from Pong C and are now reusable data
systems:

- native direct-connect text entry
- native LAN discovery result UI
- native network termination overlay
- hard-coded packet serialization for Pong actors
- hard-coded local/gamepad input assignment policy
- hard-coded play input setup adapter
- hard-coded replication channel/control message schema names

## Next Practical Step

Verify full Pong parity through the runner-backed `pong_demo`, then remove the
deprecated custom host and its opt-in CMake target.

## Definition Of Done For Pong Without `main.c`

Pong can be considered JSON/Lua-only when:

- the same generic runner binary can launch Pong from a pack or directory
- the normal Pong demo path has no custom C callback table
- standard single-player, local multiplayer, direct connect, LAN discovery,
  options, pause, haptics, audio, and rendering behavior still work
- Pong-specific gameplay behavior remains in `demos/pong/data/scripts/pong.lua`
  and authored JSON
- reusable runtime behavior lives in engine modules, not demo host code
