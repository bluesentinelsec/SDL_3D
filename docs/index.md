# Slayer 3D Documentation

Slayer 3D documentation is organized around the engine's current direction:
data-authored games running on a generic SDL-powered runtime.

## Start Here

- [Engine Overview](engine-overview.md): project purpose, architecture, and
  current alpha expectations.
- [Project Layout Guidance](project-layout.md): recommended data-project
  structure for games built with JSON, Lua, and assets.
- [Data-Only Games](data-only-games.md): responsibilities of JSON, Lua, C, and
  the generic runner.

## Authoring

- [Game Data JSON](game-data-json.md): root game schema, fragments, scenes,
  actors, input, logic, arcade shooter primitives, networking, and validation.
- [Retained UI Widgets](ui-widgets.md): data-authored UI layout, styling,
  dropdowns, dynamic text, and editor/game UI guidance.
- [Gameplay Lua API](game-data-lua.md): Lua adapter model and runtime helper
  surface.
- [Standard Options Package](standard-options.md): reusable display, audio,
  keyboard, mouse, and gamepad options screens.
- [Storage](storage.md): `user://` and `cache://` writable data policy.
- [Assets And Packs](assets.md): asset resolver, `.slayer3dpak`, embedded packs,
  compression, and obfuscation.

## Runtime And Engine Systems

- [Data Game Runtime](data-game-runtime.md): managed runtime responsibilities,
  runner integration, and managed networking.
- [Generic Game Data Model](game-data-model.md): engine-level data model
  boundaries and reusable primitive categories.

## Maintenance

- [Testing Strategy](testing.md)
- [Error Handling Strategy](error-handling.md)
- [Logging Strategy](logging.md)

## Documentation Policy

Keep docs project-agnostic by default. Demo-specific files may exist under a
demo directory when they describe how to run or maintain that demo, but root
engine docs should describe reusable engine behavior and use neutral examples.
