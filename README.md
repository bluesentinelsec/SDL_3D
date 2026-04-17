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

## Install

```sh
cmake --install build/release
```

## Formatting

```sh
./scripts/check_clang_format.sh
```
