# Pong C Host Audit

This audit tracks the remaining authored-data literals in `demos/pong/main.c`
after the Pong data-driven migration. The demo host should own SDL process
integration, native UI surfaces, and UDP session lifetime. Gameplay rules and
reusable schema decisions should live in JSON or Lua.

## Migrated in this pass

- Discovery overlay scene-state keys now resolve through
  `network.scene_state.discovery`.
- Network host actions now resolve through `network.runtime_bindings.actions`.
- Network host signals now resolve through `network.runtime_bindings.signals`.

## Remaining accepted host integration

- `network.session_flow` semantic names such as `play`, `join`, `host_lobby`,
  `match_mode`, and `network_role` are host orchestration concepts. The C host
  passes semantic names to generic resolver APIs and uses authored fallbacks only
  as defensive defaults.
- `network.runtime_bindings` semantic names such as `state_snapshot`,
  `client_input`, and `disconnect` are not game schema names. They are stable
  host integration roles that resolve to authored replication channels and
  control messages.
- Direct-connect text entry and discovery panels are still native UI overlays.
  They remain in C until the engine has data-authored text entry/list widgets
  capable of replacing those temporary surfaces.
- UDP socket/session lifecycle, timeout handling, and app shutdown remain C
  responsibilities because they bridge platform/runtime services to game data.

## Follow-up Candidates

- Replace the native direct-connect and discovery overlays with generalized
  data-authored UI widgets once editable text boxes and dynamic list controls
  exist.
- Consider caching resolved network/session binding strings if profiling ever
  shows repeated JSON lookup overhead in host orchestration.
