# SDL3D Assets

SDL3D game data should use stable virtual paths such as `asset://scripts/pong.lua`.
At runtime, `sdl3d_asset_resolver` maps those paths to mounted source
directories, `.sdl3dpak` files, or embedded memory packs.

The resolver is intentionally read-only. Build-time tools create packs; runtime
code mounts and reads them.

## Runtime Loading

Use a resolver when loading data-driven games:

```c
sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
sdl3d_asset_resolver_mount_pack_file(assets, "game.sdl3dpak", error, sizeof(error));
sdl3d_game_data_load_asset(assets, "asset://pong.game.json", session, &runtime, error, sizeof(error));
sdl3d_asset_resolver_destroy(assets);
```

Later mounts override earlier mounts. This gives the engine a clean path for
future patch or mod overlays without changing authored asset paths.

## Pack Files

`sdl3d_asset_pack_write_file()` writes deterministic uncompressed packs from an
explicit list of files. It normalizes and sorts asset paths, rejects duplicates,
and writes explicit little-endian metadata instead of raw C structs.

The format currently defers compression, encryption, and patch metadata. Those
features should be added behind the pack writer/resolver boundary.

## CMake Workflow

Include `SDL3DAssets.cmake` through the top-level build and use:

```cmake
sdl3d_add_embedded_asset_pack(my_assets
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    SYMBOL my_game_assets
    FILES
        game.game.json
        scripts/rules.lua)

target_link_libraries(my_demo PRIVATE my_assets)
```

This creates a generated object library containing `my_game_assets` and
`my_game_assets_size`, suitable for `sdl3d_asset_resolver_mount_memory_pack()`.

For native build-time `.sdl3dpak` files:

```cmake
sdl3d_add_asset_pack(my_asset_pack
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    OUTPUT "${CMAKE_BINARY_DIR}/my_game.sdl3dpak"
    FILES
        game.game.json
        scripts/rules.lua)
```

For Emscripten tests or demos that still use loose source directories:

```cmake
sdl3d_target_preload_asset_directory(my_target "${CMAKE_CURRENT_SOURCE_DIR}/data" "/data")
```
