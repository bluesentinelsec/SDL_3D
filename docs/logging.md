# Logging Strategy

Slayer 3D uses SDL's logging system directly.

- Slayer 3D logs through a dedicated custom SDL log category.
- Callers control verbosity with SDL log priorities.
- Callers may install their own SDL log output callback with `SDL_SetLogOutputFunction()`.
- Slayer 3D does not introduce a parallel logging abstraction.

Recommended usage:

- Set the Slayer 3D category priority with `slayer3d_set_log_priority()` or directly with `SDL_SetLogPriority(slayer3d_log_category(), ...)`.
- Route output globally through SDL if your application needs custom sinks, formatting, or integration with another logging backend.
- Keep messages single-line and structured enough to be useful in CI logs.

## Editor Log Parity

The standalone Slayer3D editor enables
`slayer3d_data_game_runtime_desc.mirror_logs_to_editor_console`. While that
runtime is active, every SDL message that reaches the configured SDL output
callback is also queued for the in-editor console. The original SDL callback is
still called, so terminal output and the GUI console receive the same message
and priority. The queue is bounded and thread-safe; worker-thread diagnostics
are appended to editor state on the main update thread.

Editor-authored `console.write` actions and C-owned editor events use one paired
publish path. It appends the message to editor history immediately and emits it
through SDL once. These messages use a dedicated SDL category so the mirror can
recognize that the GUI copy already exists and avoid duplicates.

Editor console history stores severity alongside text under
`editor.console.historyN.level` and exposes the visible severity as
`editor.console.lineN.level`. Values are `trace`, `verbose`, `debug`, `info`,
`warning`, `error`, or `critical`.

Substantial, completed user actions should log once with useful identifiers and
measurements. Examples include tool selection, brush creation, committed
transforms, save/open/validation results, material and skybox changes, and
panel lifecycle actions. Pointer motion, hover changes, live previews, and
routine frame updates should not emit operator-facing log entries.
