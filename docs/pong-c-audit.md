# Pong C Host Audit

This audit records the Pong host offboarding. Pong now builds from the generic
SDL3D runner source with embedded Pong assets and a default root data asset.
There is no `demos/pong/main.c` compatibility host and no Pong-specific C
callback table.

## Current Status

Pong gameplay is now data driven:

- match rules, scoring, win/loss prompts, restarts, serve behavior, ball jitter,
  AI paddle movement, and presentation effects live in JSON/Lua
- options, title, pause, direct-connect, LAN discovery, lobby, and termination
  UI are authored scene/menu data
- input bindings and device-assignment policy are authored profiles
- host-to-client snapshots, client-to-host input, and control messages use the
  generic authored network replication schema
- direct-connect and LAN discovery forms/lists use generic text-entry,
  dynamic-list, runtime-collection, and network data actions
- no native UI context or custom host callback table remains for Pong

The old custom host has been removed. `pong_demo` compiles
`tools/sdl3d_runner.c` with Pong's embedded pack symbols and
`asset://pong.game.json` as build-time defaults.

## Offboarded C Surface

The following responsibilities no longer live in Pong C.

### SDL Process And Asset Bootstrap

This work has moved to `sdl3d_runner` and `sdl3d_data_game_runtime`. The
normal `pong_demo` target supplies only build-time defaults: embedded Pong
assets and `asset://pong.game.json`.

### Generic Data Game Loop

The first reusable runtime slice now lives in `sdl3d_data_game_runtime`. Pong no
longer owns asset resolver creation, game-data loading, app-flow/frame state,
presentation caches, haptics policy wiring, or authored frame rendering
directly.

The generic runner owns the normal Pong loop through
`sdl3d_data_game_runtime`.

- input profile hotplug refresh
- network session updates
- game-data frame update/render
- cleanup for remaining session state

Games opt into standard phases and policies from JSON instead of implementing
per-game callbacks.

### Managed Network Session Orchestration

The generic data-game runtime has an opt-in managed network path that covers
this responsibility for `sdl3d_runner` and the normal `pong_demo`:

- host session creation/destruction and lobby state publishing
- direct-connect session lifetime and status publishing
- per-frame UDP session updates
- packet receive/dispatch for control, input, and snapshots
- transition to the network play scene
- disconnect/timeout handling
- pause/resume synchronization
- authoritative host snapshot publishing
- client input publishing

Packet content and orchestration are data driven. The managed runtime path is
driven by authored `network.session_flow`, `network.runtime_bindings`, and
`network.scene_state`, using standard semantics such as `host`,
`direct_connect`, `state_snapshot`, `client_input`, `start_game`, and
`disconnect`.

### Runtime State Publication

Runtime state publication for host lobby status, direct-connect status, match
termination messages, and network role/flow handoff now runs through authored
data actions and managed runtime bindings in the normal runner path.

### Diagnostic Logging

Snapshot diagnostic content and logging policy are schema-driven. The authored
`network.diagnostics.snapshots` entry controls the channel, cadence, log level,
session-state inclusion, and message template.

## Pong-Specific C Surface

There is no remaining Pong-specific host C. Pong-specific gameplay behavior is
authored in `demos/pong/data/scripts/pong.lua`, and reusable composition lives
in `demos/pong/data/pong.game.json` plus scene JSON files.

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

## Runner Parity Validation

The runner-backed path should be validated through:

```sh
build/debug/demos/pong/pong_demo
build/debug/sdl3d_runner --root demos/pong/data --data asset://pong.game.json
build/debug/sdl3d_runner --pack build/debug/demos/pong/pong.sdl3dpak --data asset://pong.game.json
```

All paths should log:

```text
SDL3D runner loaded data asset: asset://pong.game.json
```

## Definition Of Done For Pong Without `main.c`

Pong is considered JSON/Lua-only when:

- the same generic runner binary can launch Pong from a pack or directory
- the normal Pong demo path has no custom C callback table
- standard single-player, local multiplayer, direct connect, LAN discovery,
  options, pause, haptics, audio, and rendering behavior still work
- Pong-specific gameplay behavior remains in `demos/pong/data/scripts/pong.lua`
  and authored JSON
- reusable runtime behavior lives in engine modules, not demo host code
