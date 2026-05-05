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

The largest remaining custom surface is multiplayer session orchestration:

- host session creation/destruction and lobby state publishing
- direct-connect session lifetime and status publishing
- per-frame UDP session updates
- packet receive/dispatch for control, input, and snapshots
- transition to the network play scene
- disconnect/timeout handling
- pause/resume synchronization
- authoritative host snapshot publishing
- client input publishing

Most packet content is already data driven. What remains is the transport
session flow. This should become an engine-managed network session runner driven
by authored `network.session_flow` and `network.runtime_bindings`.

### Runtime State Publication

Pong C still publishes scene-state values for host lobby status, direct-connect
status, match termination messages, and network role/flow handoff.

These are generic UI/runtime status concepts. The runner should provide data
actions or managed bindings for session status, peer lists, selected peers,
termination reasons, and return scenes.

### Diagnostic Logging

Snapshot diagnostics are schema-driven, but the host still decides when to log
Pong multiplayer state. A generic runtime can expose an authored diagnostics
policy for network sessions and replication channels.

## Remaining Pong-Specific C Literals

The remaining `PONG_*` constants are mostly semantic runtime binding names such
as `state_snapshot`, `client_input`, `start_game`, `pause_request`, and
`direct_connect`. They are not hard-coded actor/property packet schemas, but
they still prove that the current binary is a Pong host rather than a generic
runner.

A runner should either:

- use standard semantic binding names defined by the engine, or
- read a small authored runtime profile that declares which session, action,
  signal, replication, and control roles the standard network loop should use.

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

Deprecate the custom Pong host by adding a generic SDL3D runner/runtime.

Recommended first runtime slice:

1. Add a small runner executable that launches a game data asset or pack through
   that API.
2. Convert `demos/pong/main.c` into either a thin compatibility wrapper around
   the runner API or remove it from the normal path once the runner can launch
   Pong.
3. Keep multiplayer orchestration in Pong C only until the next slice moves it
   into a managed network-session runtime.

## Definition Of Done For Pong Without `main.c`

Pong can be considered JSON/Lua-only when:

- the same generic runner binary can launch Pong from a pack or directory
- Pong has no custom C callback table
- standard single-player, local multiplayer, direct connect, LAN discovery,
  options, pause, haptics, audio, and rendering behavior still work
- Pong-specific gameplay behavior remains in `demos/pong/data/scripts/pong.lua`
  and authored JSON
- reusable runtime behavior lives in engine modules, not demo host code
