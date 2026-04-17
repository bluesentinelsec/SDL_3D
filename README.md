# SDL3D

SDL3D is an idiomatic C17 project scaffold for a high-level 3D graphics API powered by SDL3's GPU API.

SDL3D builds as a static library and has a hard dependency on SDL3.

## Layout

- `include/` public headers
- `src/` library sources
- `tests/` executable tests registered with CTest
- `cmake/` reusable CMake modules and package configuration templates
- `scripts/` repository maintenance scripts

## Testing

Unit tests use GoogleTest. CMake will use an installed `GTest` package when available and otherwise fetch GoogleTest automatically when `SDL3D_BUILD_TESTS=ON`.
Testing and renderer-loop policy are documented in [docs/testing.md](docs/testing.md).
Error propagation conventions are documented in [docs/error-handling.md](docs/error-handling.md).
Logging conventions are documented in [docs/logging.md](docs/logging.md).

## Dependencies

- SDL3 3.2 or newer, discovered through CMake's config packages

SDL3D links publicly against `SDL3::SDL3`, which is the SDL target guaranteed by the official SDL CMake package. That keeps SDL3D compatible with caller projects that choose either static or shared SDL builds.

If SDL3 is not already available from a parent project or `find_package(SDL3 ...)`, SDL3D can fetch SDL3 automatically with `FetchContent`. The fallback currently pins the latest stable SDL3 release tag, `release-3.4.0`, published on January 1, 2026. Set `SDL3D_FETCH_SDL3=OFF` if you want SDL3D to require a caller-provided SDL3 instead.

When SDL3 is fetched this way, SDL3D disables its own install/export package generation for that build. Normal install/export packaging remains available when SDL3 comes from a parent target or a discovered SDL3 package.

## Build

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Top-level builds generate `compile_commands.json` automatically for LSP and other tooling integrations.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

For a sanitizer-enabled build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
CC=clang cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

## Install

```sh
cmake --install build/release
```

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

When SDL3D is consumed as a subproject, its tests default to `OFF` so it behaves cleanly inside a parent build. To force them on, set `SDL3D_BUILD_TESTS=ON` before `FetchContent_MakeAvailable`. SDL3D will use a parent-provided `SDL3::SDL3` target first, then `find_package(SDL3 ...)`, and finally its SDL3 fetch fallback when enabled.

## Formatting

```sh
./scripts/check_clang_format.sh
```
