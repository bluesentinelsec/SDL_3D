# SDL3D

SDL3D is an idiomatic C17 project scaffold for a high-level 3D graphics API powered by SDL3's GPU API.

## Layout

- `include/` public headers
- `src/` library sources
- `tests/` executable tests registered with CTest
- `cmake/` reusable CMake modules and package configuration templates
- `scripts/` repository maintenance scripts

## Build

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

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

When SDL3D is consumed as a subproject, its tests default to `OFF` so it behaves cleanly inside a parent build. To force them on, set `SDL3D_BUILD_TESTS=ON` before `FetchContent_MakeAvailable`.

## Formatting

```sh
./scripts/check_clang_format.sh
```
