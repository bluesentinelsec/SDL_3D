# SDL3D

SDL3D is an alpha-stage, data-driven, general-purpose game engine powered by
SDL. The project is intended to support both 2D and 3D games while keeping
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

SDL3D is not 1.0. APIs, schemas, and authored data conventions may still change
when correctness or engine clarity requires it.

## Project Status

The engine currently builds as a C17 static library plus tools and demo
targets. The generic `sdl3d_runner` can launch games from:

- a development asset directory
- a `.sdl3dpak` file
- an embedded asset pack supplied by the build

The demo games are compatibility proofs for engine capabilities. New demo work
should strengthen reusable systems instead of adding one-off host code.

## Repository Layout

- `include/`: public C headers
- `src/`: engine implementation
- `tools/`: command-line tools such as `sdl3d_runner` and `sdl3d_pack`
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

SDL3D links publicly against `SDL3::SDL3`, the SDL target guaranteed by the
official SDL CMake package. If SDL3 is not already available from a parent
project or `find_package(SDL3 ...)`, SDL3D can fetch SDL3 automatically with
`FetchContent`. Set `SDL3D_FETCH_SDL3=OFF` if you want SDL3D to require a
caller-provided SDL3 instead.

When SDL3 is fetched this way, SDL3D disables its own install/export package
generation for that build. Normal install/export packaging remains available
when SDL3 comes from a parent target or a discovered SDL3 package.

Vendored dependencies live under `vendor/` and are built from source. They are
kept static-link friendly and should not introduce runtime deployment
requirements for engine users or SDL3D tools.

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

For a sanitizer-enabled build:

```sh
CC=clang cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

## Run A Data-Authored Game

From a development directory:

```sh
build/debug/sdl3d_runner --root path/to/game/data --data asset://game.game.json
```

From a pack file:

```sh
build/debug/sdl3d_runner --pack path/to/game.sdl3dpak --data asset://game.game.json
```

Demo targets may also build a game-specific executable from the same generic
runner source with embedded assets and a default root data asset. That wrapper
must not contain game-specific rules.

## Consume With FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    SDL3D
    GIT_REPOSITORY git@github.com:bluesentinelsec/SDL_3D.git
    GIT_TAG main
)

FetchContent_MakeAvailable(SDL3D)

target_link_libraries(your_target PRIVATE SDL3D::sdl3d)
```

When SDL3D is consumed as a subproject, its tests default to `OFF`. To force
them on, set `SDL3D_BUILD_TESTS=ON` before `FetchContent_MakeAvailable`.

## Formatting

Run clang-format before submitting C or C++ changes:

```sh
./scripts/check_clang_format.sh
```
