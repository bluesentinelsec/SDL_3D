# Engine Overview

SDL3D is an alpha-stage game engine built in C on top of SDL. Its intended
shape is a data-driven, general-purpose runtime for both 2D and 3D games.

## Goals

- Make complete games primarily from JSON, Lua, and assets.
- Keep the runner and managed game loop game-agnostic.
- Provide reusable primitives for common game systems instead of one-off demo
  host code.
- Support both low-level C API use and higher-level data-authored games.
- Grow reusable templates for common game-development patterns, including
  retro visual profiles, dynamic lighting, sprite-heavy 2D games, fixed-screen
  arcade games, and Doom/Quake-like 3D worlds.

## Current Architecture

SDL3D is layered:

1. Platform and loop: SDL window, renderer, timing, events, input, audio, and
   the outer callback loop.
2. Engine primitives: properties, actors, signals, timers, logic, input,
   storage, assets, rendering, audio, haptics, networking, and persistence.
3. Data game runtime: JSON/Lua runtime loading, presentation caches, authored
   frame update/render, input profile refresh, managed network orchestration,
   and ordered cleanup.
4. Generic runner: command-line executable that mounts assets and runs a root
   game JSON file without knowing the game.
5. Game content: JSON fragments, scene files, Lua scripts, and media assets.

## Data Boundary

- JSON authors reusable engine primitives and composition.
- Lua owns game-exclusive rules and calculations.
- C owns reusable engine capabilities and platform integration.

If the engine already has the right primitive, prefer JSON. If behavior is
specific to one game and not worth promoting to an engine primitive, use Lua. If
several games need the same capability and Lua would be an awkward workaround,
add or improve the engine primitive in C and expose it back through JSON/Lua.

## Alpha Expectations

SDL3D is not yet 1.0. Breaking changes are acceptable when they improve
correctness, remove demo-specific assumptions, or clarify the API. When
behavior changes, update the demos, tests, and docs together.
