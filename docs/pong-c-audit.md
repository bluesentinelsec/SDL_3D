# Pong C Host Audit

This audit records the state of `demos/pong/main.c` after the multiplayer UI
migration. The long-term target is for Pong to be authored as JSON, Lua, and
assets loaded by a generic SDL3D runner. In that model, a game does not need a
custom `my_game/main.c`; it supplies data to the engine runtime.

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

The remaining Pong C is no longer gameplay-rule code. It is mostly a custom
runtime host that wires generic engine systems together for this one demo.

## Remaining C Surface

`demos/pong/main.c` is still large because it owns several responsibilities that
should move behind reusable engine/runtime APIs.

### SDL Process And Asset Bootstrap

The file still creates the SDL3D game config, mounts embedded/pack/directory
assets, loads `asset://pong.game.json`, initializes caches, starts text input,
and calls `sdl3d_game_run`.

This is generic runner work. A reusable SDL3D runtime should accept a game data
asset path or pack path, load app/window/audio settings from game data, mount
assets, and run without game-specific C.

### Generic Data Game Loop

The first reusable runtime slice now lives in `sdl3d_data_game_runtime`. Pong no
longer owns asset resolver creation, game-data loading, app-flow/frame state,
presentation caches, haptics policy wiring, or authored frame rendering
directly.

`pong_tick`, `pong_pause_tick`, `pong_render`, and `pong_shutdown` still exist,
but they are now compatibility callbacks around generic runtime calls plus
Pong's remaining network orchestration:

- input profile hotplug refresh
- network session updates
- game-data frame update/render
- cleanup for remaining session state

These should become a managed data-game loop in the engine runtime. A game
package should opt into standard phases and policies from JSON instead of
implementing per-game callbacks.

### Managed Network Session Orchestration

Pong's legacy host still contains multiplayer session orchestration, but the
generic data-game runtime now has an opt-in managed network path that covers
this responsibility for `sdl3d_runner`:

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

Pong C still publishes scene-state values for host lobby status, direct-connect
status, match termination messages, and network role/flow handoff.

These are generic UI/runtime status concepts. The runner should provide data
actions or managed bindings for session status, peer lists, selected peers,
termination reasons, and return scenes.

### Diagnostic Logging

Snapshot diagnostic content and logging policy are now schema-driven. Pong C
only notifies the engine that a generic network snapshot event occurred; the
authored `network.diagnostics.snapshots` entry controls the channel, cadence,
log level, session-state inclusion, and message template.

## Remaining Pong-Specific C Literals

The remaining `PONG_*` constants in the compatibility host are mostly semantic runtime binding names such
as `state_snapshot`, `client_input`, `start_game`, `pause_request`, and
`direct_connect`. They are not hard-coded actor/property packet schemas, but
they still prove that the current binary is a Pong host rather than a generic
runner.

The generic runner now uses standard semantic binding names defined by the
engine. The remaining task is replacing the demo binary path with the runner
path and deleting the compatibility host when parity has been verified.

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

Verify full Pong parity through `sdl3d_runner`, then convert `demos/pong` to
launch through the generic runner or keep only a tiny build-time wrapper for
asset defaults.

## Definition Of Done For Pong Without `main.c`

Pong can be considered JSON/Lua-only when:

- the same generic runner binary can launch Pong from a pack or directory
- Pong has no custom C callback table
- standard single-player, local multiplayer, direct connect, LAN discovery,
  options, pause, haptics, audio, and rendering behavior still work
- Pong-specific gameplay behavior remains in `demos/pong/data/scripts/pong.lua`
  and authored JSON
- reusable runtime behavior lives in engine modules, not demo host code
