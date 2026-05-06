# Pong Template Checklist

Use this checklist when starting a new SDL3D game from the Pong template.
It keeps reusable engine data separate from game-specific content and makes
scene handoff behavior explicit.

## Recommended project shape

- `game.json` at the project root for startup policy, storage, and standard
  game-wide config.
- `scenes/*.scene.json` for title, options, play, pause, splash, and level
  scenes.
- `scripts/*.lua` for game-specific behavior that should stay outside the
  engine.
- `assets/` for textures, audio, icons, and other raw source media.
- `pack/` or generated asset pack output for packaged runtime delivery.
- `user://` storage config for settings, profiles, and save data.

## What belongs in reusable engine data

- Window and renderer policy.
- Scene transition policy.
- Managed loop policy.
- Standard options menus and their controls.
- Input bindings and defaults.
- Scene-state handoff keys and return-scene behavior.
- Persistence paths and schema names.

## What should stay game-specific

- Scoring, rules, and win/lose conditions.
- Scene order and the set of scenes a game actually ships.
- Game-specific Lua scripts.
- Unique assets, music, and effects.
- Game-specific menu labels and custom settings categories.

## New-game checklist

1. Define the startup config in `game.json`.
2. Author a splash scene if the game needs one.
3. Author the title scene and point it at the initial play scene.
4. Add the standard options package if the game uses common settings screens.
5. Wire input defaults for keyboard, mouse, and gamepad.
6. Define any persistent scene-state keys before using them in transitions.
7. Keep scene return behavior explicit:
   - submenu `Back` returns to the parent options scene
   - top-level `Back` returns to the title or caller-provided return scene
8. Put game-specific rules in Lua or game data, not in the host unless the
   behavior truly cannot be generalized.
9. Add tests for any new reusable behavior.

## Scene handoff rules

- Use authored scene transitions when a scene needs a fade, delay, or outro.
- Use scene-state for values that must survive a transition, such as the
  selected options page, the last play scene, or a restart target.
- Treat `return_scene` as an authored fallback for leaving a top-level flow,
  not as a substitute for submenu navigation.

## Practical target

If the template is healthy, a new arcade demo should be able to start with:

- a `game.json`
- a small set of scene JSON files
- one or more Lua scripts
- mostly assets and authored data
- no game-specific host code when the generic runner covers the needed systems

Custom C should mean the game has discovered a missing reusable engine feature.
After that feature lands, expose it through JSON/Lua so future games do not
need their own host code for the same behavior.
