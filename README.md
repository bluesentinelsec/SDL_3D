# Slayer 3D

Slayer 3D is an alpha-stage, data-driven, general-purpose game engine powered by
SDL and OpenGL. The project is intended to support both 2D and 3D games while keeping
game-specific content in JSON, Lua, and assets whenever the engine already
provides the required reusable primitives.

The current engine direction is:

- a generic runtime/runner that can launch data-authored games
- JSON for reusable engine primitives and authored composition
- Lua for game-specific rules, calculations, and policy
- C for engine systems, platform integration, renderer backends, and reusable
  performance-critical primitives
- templates and patterns for common game-development use cases such as retro
  renderer profiles, dynamic lighting, sprite-heavy 2D games, fixed-screen
  arcade games, and Doom/Quake-like 3D worlds

Slayer 3D is not 1.0. APIs, schemas, and authored data conventions may still change
when correctness or engine clarity requires it.

This rebrand is a hard pre-1.0 rename from the former SDL3D identifiers to the
canonical Slayer 3D identifiers. Compatibility aliases for the old C API,
CMake options, tool names, pack extension, and JSON schema names are not kept;
authored projects should migrate to `slayer3d_`, `SLAYER3D_`,
`Slayer3D::slayer3d`, `.slayer3dpak`, and `slayer3d.*` schema names.

## Project Status

The engine currently builds as a C17 static library plus tools and demo
targets. The generic `slayer3d_runner` can launch games from:

- a development asset directory
- a `.slayer3dpak` file
- an embedded asset pack supplied by the build
- a fused executable produced by appending a pack to the generic runner

The demo games are compatibility proofs for engine capabilities. New demo work
should strengthen reusable systems instead of adding one-off host code.
The repository also includes data-only dojos for focused primitive and mechanic
tuning, including FPS mechanics, procedural mesh primitives, and sector-local
lighting showcases.

Default authored display and world conventions are intentionally simple:
Slayer 3D games target a 1280x720 logical canvas that is letterboxed to preserve
aspect ratio on the user's display, one world unit represents one meter, and
perspective/FPS cameras default to a 90 degree horizontal field of view unless a
game authors a different value.

## Repository Layout

- `include/`: public C headers
- `src/`: engine implementation
- `tools/`: command-line tools such as `slayer3d_runner`, `slayer3d_editor`,
  `slayer3d_pack`, and `slayer3d_bundle`
- `demos/`: data-authored and low-level capability demos
- `docs/`: engine documentation
- `tests/`: GoogleTest and renderer tests registered with CTest
- `cmake/`: reusable CMake modules and package configuration templates
- `scripts/`: repository maintenance scripts
- `vendor/`: vendored C dependencies

## Documentation

Start here:

- [Documentation Index](docs/index.md)
- [Engine Overview](docs/engine-overview.md)
- [Project Layout Guidance](docs/project-layout.md)
- [Data-Only Games](docs/data-only-games.md)
- [Game Data JSON](docs/game-data-json.md)
- [Gameplay Lua API](docs/game-data-lua.md)
- [Data Game Runtime](docs/data-game-runtime.md)
- [Assets And Packs](docs/assets.md)
- [Testing Strategy](docs/testing.md)

## Dependencies

- SDL3 3.2 or newer, discovered through CMake config packages

Slayer 3D links publicly against `SDL3::SDL3`, the SDL target guaranteed by the
official SDL CMake package. If SDL3 is not already available from a parent
project or `find_package(SDL3 ...)`, Slayer 3D can fetch SDL3 automatically with
`FetchContent`. Set `SLAYER3D_FETCH_SDL3=OFF` if you want Slayer 3D to require a
caller-provided SDL3 instead.

When SDL3 is fetched this way, Slayer 3D disables its own install/export package
generation for that build. Normal install/export packaging remains available
when SDL3 comes from a parent target or a discovered SDL3 package.

Vendored dependencies live under `vendor/` and are built from source. They are
kept static-link friendly and should not introduce runtime deployment
requirements for engine users or Slayer 3D tools.

## Build

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Top-level builds generate `compile_commands.json` automatically for LSP and
other tooling integrations.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

For an optimized editor-only build:

```sh
cmake --preset editor
cmake --build --preset editor
```

Use this preset when running or evaluating the editor. The default preset is an
unoptimized engine development build.

For a sanitizer-enabled build:

```sh
CC=clang cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

## Run A Data-Authored Game

From a development directory:

```sh
build/debug/slayer3d_runner --root path/to/game/data --data asset://game.game.json
```

From a pack file:

```sh
build/debug/slayer3d_runner --pack path/to/game.slayer3dpak --data asset://game.game.json
```

As a single fused executable:

```sh
build/debug/slayer3d_bundle \
  --runner build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --output build/MyGame

build/MyGame
```

Start directly in a scene for development:

```sh
build/debug/slayer3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --scene scene.level_1 \
  --state checkpoint=midboss
```

Demo targets may also build a game-specific executable from the same generic
runner source with embedded assets and a default root data asset. That wrapper
must not contain game-specific rules.

## Consume With FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    Slayer3D
    GIT_REPOSITORY git@github.com:bluesentinelsec/Slayer3D.git
    GIT_TAG main
)

FetchContent_MakeAvailable(Slayer3D)

target_link_libraries(your_target PRIVATE Slayer3D::slayer3d)
```

When Slayer 3D is consumed as a subproject, its tests default to `OFF`. To force
them on, set `SLAYER3D_BUILD_TESTS=ON` before `FetchContent_MakeAvailable`.

## Formatting

Run clang-format before submitting C or C++ changes:

```sh
./scripts/check_clang_format.sh
```
