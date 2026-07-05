# Testing Strategy

Slayer 3D uses two complementary test layers.

## Unit Tests

- Use GoogleTest for API-level behavior.
- Cover argument validation, SDL-style error propagation, and deterministic pure-library behavior.
- Keep these tests fast and isolated.

## Renderer Loop Tests

- Exercise rendering through a real SDL window and renderer.
- Use a deterministic loop with a fixed frame budget and a timeout-based escape hatch.
- Poll events each frame and fail if the loop receives an unexpected quit event.
- Keep rendering work minimal so the tests stay CI friendly while still validating real presentation paths.

## Editor Scene Data Validation

- `python3 tools/validate_editor_scene.py` checks the repeated-widget
  invariants in the editor shell scene data: inspector property row slots stay
  contiguous with consistent geometry and slot bindings, scroll thumbs stay
  inside their scroll tracks, and authored data does not reference retired
  surfaces (the `editor_shell_dojo` naming, the legacy palette modal, or the
  removed `editor.file.edit.focus` state).
- Run it after editing `apps/slayer3d_editor/data/`.

## CI

- Windows and macOS run renderer tests normally.
- Linux runs tests under `xvfb-run` so real SDL rendering still works in CI without changing the test code.
