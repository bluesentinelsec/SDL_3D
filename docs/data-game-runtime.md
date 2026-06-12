# Data Game Runtime

`slayer3d_data_game_runtime` is the engine-owned runtime layer for games authored
primarily with JSON, Lua, and assets. It exists so games can use the generic
runner instead of implementing repetitive per-game asset, scene, app-flow,
presentation, and network lifecycle code.

The runtime is game-agnostic. It does not know scene names, actor names, input
actions, or replication channels for a specific game.

Sector/FPS games use the same runtime path as fixed-screen or arcade demos.
Authored `sector_levels`, `controller.fps_sector`, `fps` cameras,
`sector_doors`, `sector_platforms`, `sensor.sector`, render profiles, and
ordinary scene/menu data are consumed by the generic runtime and renderer; they
do not require a game-specific native host.

## Responsibilities

The runtime currently owns:

- asset resolver creation
- caller-provided asset mounting
- root game JSON loading
- font, image, particle, sprite, model, mesh primitive, and asset warmup
  presentation caches
- authored app-flow startup and frame update
- authored frame render through `slayer3d_game_data_draw_frame`
- haptics policy signal wiring
- input-profile hotplug refresh state and automatic gamepad-count rebinding
- runtime-owned direct-connect, host, and discovery session primitives exposed
  through data actions
- runtime-bound host/client packet loops for authored control messages,
  client input replication, authoritative snapshots, and pause-state sync
- optional managed network orchestration for authored host lobby,
  direct-connect, start-game, pause/resume, disconnect, timeout, termination
  acknowledgement, client camera-toggle, status publication, and snapshot
  diagnostics
- ordered cleanup

The generic `slayer3d_runner` executable owns the outer SDL process loop for games
that only need the current data runtime surface. It enables managed network
orchestration so data-authored games can use the standard direct-connect/LAN
flow without game-specific C callbacks.

## Startup Shape

A host creates a normal `slayer3d_game_session`, fills a
`slayer3d_data_game_runtime_desc`, and calls
`slayer3d_data_game_runtime_create`.

The descriptor requires:

- `session`: the game session that provides input, signals, timers, logic, and
  optional audio
- `data_asset_path`: the root game JSON asset path, for example
  `asset://game.game.json`

The descriptor may also include:

- `media_dir`: built-in font/media root
- `mount_assets`: callback used to mount a directory, pack, or embedded pack
- `mount_userdata`: user data passed to `mount_assets`
- `enable_managed_network`: opt-in authored host/direct-connect orchestration
  for games that provide the standard network semantics below

## Frame Use

During the managed loop:

- call `slayer3d_data_game_runtime_update_frame` from tick and pause tick paths
- call `slayer3d_data_game_runtime_render` from the render callback
- input-profile hotplug refresh runs automatically inside
  `slayer3d_data_game_runtime_update_frame`
- when `enable_managed_network` is true, host/client session updates and
  snapshot publication also run automatically inside
  `slayer3d_data_game_runtime_update_frame`

The outer loop still controls fixed timestep, SDL events, input snapshots,
audio frame updates, and presentation.

## Presentation Asset Warmup

The managed runtime owns a `slayer3d_game_data_asset_warmup_queue` and passes it
to `slayer3d_game_data_draw_frame`. The presentation layer enumerates active
scene UI images, brush material textures, render sprite assets, and render model
assets once per scene activation. When the active scene changes, unfinished
requests from the previous activation are canceled so the current scene's assets
take priority. Requests are deduplicated and then serviced with a small
per-frame budget instead of blocking startup on the full scene asset set.

On native SDL targets, the managed runtime also starts a background warmup worker
for CPU-only texture, UI image, authored sprite, and authored model preparation.
The worker reads and decodes those assets, then the frame warmup service
publishes the prepared assets into the render texture, UI image, sprite, and
model caches on the main presentation path. Web builds and platforms where
worker creation fails fall back to the same budgeted service path.

The managed runtime publishes warmup progress to scene state each render under
the `asset_warmup` prefix. Data-authored UI can bind to `asset_warmup.total`,
`queued`, `loading`, `ready_for_finalize`, `pending`, `ready`, `failed`,
`canceled`, `completed`, `worker_threads`, `service_calls`, `service_jobs`,
`progress`, `elapsed_ms`, `service_last_ms`, `service_total_ms`,
`service_max_ms`, `active`, `complete`, and `status`. Hosts that need a
different namespace can call
`slayer3d_data_game_runtime_publish_asset_warmup_stats()` with a custom prefix.
For active-scene UI images, the same publisher also writes
`asset_warmup.ui_image.<image_id>.status`, `pending`, `ready`, and `failed`
fields so authored texture browsers can show per-thumbnail loading or failure
states. UI image drawing skips matching pending/failed warmup requests instead
of forcing a synchronous lazy load. Editor texture swatches backed by pending or
failed warmup thumbnails remain visible with status text, but their selection
actions are ignored until the thumbnail is available.

Authored model assets use the same per-asset state shape under
`asset_warmup.model.<model_id>.status`, `pending`, `ready`, and `failed`.
Editor actor/model browsers can bind to those keys to show progressive model
availability while CPU mesh decoding and render-thread finalization complete in
the background.

## Network Packet Loops

The runtime network helpers are game-agnostic. Callers provide session names
and semantic binding keys, such as `state_snapshot`, `client_input`,
`start_game`, `pause_request`, `resume_request`, and `disconnect`. The helpers
resolve those keys through authored `network.runtime_bindings`, then:

- update the runtime-owned host or direct-connect transport session
- decode control packets and return result flags for scene-flow decisions
- apply client input overrides on the host
- apply host snapshots on the client
- mirror authored network pause state between host context and client context
- send client input packets while network play is active
- send host snapshots after the authoritative simulation step

The helpers intentionally report events instead of hard-coding transitions.
That keeps game-specific flow in authored JSON/Lua rather than native runner
code.

For data-only games launched by `slayer3d_runner`, prefer
`enable_managed_network`. The managed path uses standard semantic names:

- host session: `host`
- direct-connect session: `direct_connect`
- replication bindings: `state_snapshot`, `client_input`
- control bindings: `start_game`, `pause_request`, `resume_request`,
  `disconnect`
- action bindings: `menu_select`, `camera_toggle`
- signal bindings: `lobby_start`, `camera_toggle`
- session-flow scenes: `play`, `host_lobby`, `direct_connect`, `discovery`
- session-flow events: `host_start_game`, `client_start_game`,
  `client_state_before_start`, `host_match_terminated`,
  `client_match_terminated`, `host_client_disconnected`,
  `client_connection_closed`, and `network_match_termination_ack`
- managed policy: `network.session_flow.managed_runtime.enabled`,
  `termination_ack_delay_seconds`, and `keep_alive_scenes`

Games can map those semantics to their own concrete scene ids, signal names,
action names, replication channels, and control-message names with
`network.session_flow`, `network.runtime_bindings`, and `network.scene_state`.
The runner enables the engine-side managed path, but the game data must also
author `managed_runtime.enabled: true`; local-only games can omit networking
entirely or leave managed networking disabled. Keep-alive scene policy is
authored so a game can move through loading, rematch, or lobby-adjacent scenes
without dropping an otherwise healthy network session.

Network session-flow events close that gap for transition policy. Games can
author `network.session_flow.events` entries and a host or runner can execute
them with `slayer3d_game_data_run_network_session_flow_event()`. Each event may
set pause state and run ordinary data actions, so policies such as “client
received start game”, “peer disconnected during play”, or “acknowledge match
termination” can live in JSON instead of C. The runtime passes optional payload
properties, such as `reason`, into those actions; supported string fields may
use `{reason}` placeholders.

Authored `network.diagnostics.snapshots` entries make multiplayer state logging
policy data-driven as well. Runtime code can call
`slayer3d_game_data_log_network_snapshot_diagnostic()` by policy name; the game
data chooses the replication channel, log level, cadence, session-state
inclusion, and message template.

## Generic Runner

`slayer3d_runner` launches a game data asset without game-specific C callbacks.
It can mount either a development directory or a built `.slayer3dpak`, then reads
the app/window/audio config from the root game JSON before entering the managed
loop.

Development directory example:

```sh
build/debug/slayer3d_runner --root path/to/game/data --data asset://game.game.json
```

Pack-file example:

```sh
build/debug/slayer3d_runner --pack path/to/game.slayer3dpak --data asset://game.game.json
```

Fused executable example:

```sh
build/debug/slayer3d_bundle \
  --runner build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --output build/MyGame

build/MyGame
```

## Generic Editor Host

`slayer3d_editor` is a thin host over the same managed runtime and renderer. It
launches a data-authored editor project, injects editor input/output paths as
scene state, and leaves the actual tool behavior in JSON/Lua.

```sh
build/debug/slayer3d_editor new --project demos/editor_shell_dojo --output /tmp/level.fragment.json --overwrite
```

Projects are described by `slayer3d.project.json` manifests. `new` starts from
the project's empty editor scene; `open` loads an existing editable fragment and
saves back to `--input` unless `--output` is supplied:

```sh
build/debug/slayer3d_editor open --project path/to/project --input path/to/level.fragment.json
```

For the current graybox editor workflow, the saved file is a source-backed
editable level fragment. The editor source model is the truth: brush creation,
movement, resizing, duplication, deletion, save/reopen, and test-run handoff all
operate on `editor_brush_sources`, then compile runtime brush geometry from that
source. A typical blockout loop is:

```sh
build/debug/slayer3d_editor new --project demos/editor_shell_dojo --output /tmp/level.fragment.json --overwrite
```

Inside the editor, use Brush Tool and Select mode to create and edit floors,
walls, ceilings, pits, platforms, and corridors, place a player start, then save
with the normal save command. Reopen the same source fragment with:

```sh
build/debug/slayer3d_editor open --project demos/editor_shell_dojo --input /tmp/level.fragment.json
```

The editor test-run command validates the source model, warns about leaks,
applies the selected player start, and switches into the playable test-run scene.
Leak findings are warning-level during the graybox milestone, but source/compiled
identity errors remain hard failures because stale runtime brush geometry must
not become the shipped level.

The manifest currently carries `data_root`, `editor_entry`, optional
`media_root`, and optional `test_run_output`. The editor host translates those
fields into a normal runner launch and injects `editor.command`,
`editor.input.path`, `editor.save.path`, and `editor.test_run.path` as scene
state. This keeps the shell data-authored while still giving users a stable CLI:
`--project` selects the editor project, `--input` selects the editable fragment
for `open`, and `--output` selects the save target.

For low-level debugging, the same launch can be written directly with the
generic runner:

```sh
build/debug/slayer3d_runner \
  --root demos/editor_shell_dojo/data \
  --data asset://editor_shell_dojo.game.json \
  --state editor.command=open \
  --state editor.input.path=/tmp/level.fragment.json \
  --state editor.save.path=/tmp/level.fragment.json
```

The fused executable path is still the same generic runner. The game data is
stored as an appended `.slayer3dpak`, and the runner auto-mounts it only when no
explicit mount flags are provided.

Demo targets may build a game-specific executable from this same runner source
with embedded assets and a default data asset. Such wrappers should provide
only build-time defaults:

```sh
build/debug/demos/my_game/my_game_demo
```

Optional flags:

- `--media <dir>` overrides the built-in media directory used for engine fonts
  and shared media.
- `--scene <scene-id>` starts directly in a loaded scene instead of
  `scenes.initial`. This is intended for editor and developer workflows such as
  play-testing a level without sitting through splash, title, or cutscene flow.
- `--player-start <name>` applies an authored `editor_player_starts` marker
  before the first scene-enter signal. If the marker declares a scene and
  `--scene` is omitted, that scene becomes the initial scene. If both are
  supplied, they must reference the same scene.
- `--test-run-manifest <path-or-asset>` reads a
  `slayer3d.editor_test_run.v0` handoff manifest produced by editor data. The
  manifest supplies the runner `--data`, `--scene`, and/or `--player-start`
  values, while mount flags remain explicit on the runner command line. Explicit
  `--data`, `--scene`, or `--player-start` arguments must agree with manifest
  values when both are present.
- `--state <key=value>` injects one scene-state value before the direct scene
  enter signal runs. The value is parsed as JSON when possible, so
  `--state lives=3`, `--state debug=true`, `--state spawn=[1,2,3]`, and
  `--state label=checkpoint_a` are all valid. Repeat the flag for multiple
  values.
- `--state-json <object>` injects multiple scene-state values from an inline
  JSON object.
- `--state-file <path-or-asset>` injects scene-state values from a JSON object
  file. Files may be host paths or mounted `asset://` paths.
- `--embedded` is available only when a build target defines
  `SLAYER3D_RUNNER_EMBEDDED_ASSETS` and provides the generic
  `slayer3d_runner_embedded_assets` pack blob symbols.

Test-run manifests are applied before app config loading so the manifest's data
asset can become the game entry point. State inputs are applied in this order:
`--state-file`, then `--state-json`, then `--state`. Later values overwrite
earlier values with the same key.
Injected state is available to the scene's `on_enter` signal and to authored
conditions during scene entry. Direct scene launch also suppresses startup
app-flow transitions so the requested scene is immediately usable.

The generic runner source is game-agnostic: it does not reference game scene
names, actions, actors, replication channels, controls, score state, menus, or
Lua adapters. Demo-specific runner targets may supply build-time defaults such
as an embedded asset symbol and a root data asset path, but gameplay and
runtime behavior should remain authored in JSON and Lua. Any remaining need for
a custom host indicates engine runtime work that should move into reusable
data-driven systems.

## Why This Matters

This API is the runtime foundation for data-only games. See
[Data-Only Games](data-only-games.md) for the authoring contract and parity
checklist.
