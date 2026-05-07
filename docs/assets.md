# SDL3D Assets

SDL3D game data should use stable virtual paths such as
`asset://scripts/rules.lua` or `asset://images/player.png`. At runtime,
`sdl3d_asset_resolver` maps those paths to mounted source directories,
`.sdl3dpak` files, or embedded memory packs.

The resolver is intentionally read-only. Build-time tools create packs;
runtime code mounts and reads them.

## Runtime Loading

Use a resolver when loading data-authored games:

```c
sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
sdl3d_asset_resolver_mount_pack_file(assets, "game.sdl3dpak", error, sizeof(error));
sdl3d_game_data_load_asset(assets, "asset://game.game.json", session, &runtime, error, sizeof(error));
sdl3d_asset_resolver_destroy(assets);
```

Later mounts override earlier mounts. This gives the engine a clean path for
future patch or mod overlays without changing authored asset paths.

## Pack Files

`sdl3d_asset_pack_write_file()` writes deterministic packs from an explicit
list of files. The current build defaults to compressing pack bytes at rest so
disk and embedded packs stay smaller, and the resolver transparently
decompresses them on mount.

Compression is controlled by the `SDL3D_COMPRESS_PACKS` CMake option. Set it
`OFF` if you need raw packs for debugging or tooling. Pack obfuscation is
enabled by default with the CMake cache password `password`; set
`SDL3D_PACK_PASSWORD` to an empty string to disable the wrapper. The writer adds
obfuscation after compression, and the resolver unwraps it on mount. This is
intended to prevent casual asset extraction, not to provide hardened security.

## CMake Workflow

For embedded packs:

```cmake
sdl3d_add_embedded_asset_pack(my_assets
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    SYMBOL my_game_assets
    FILES
        game.game.json
        scripts/rules.lua)

target_link_libraries(my_game PRIVATE my_assets)
```

This creates a generated object library containing `my_game_assets` and
`my_game_assets_size`, suitable for `sdl3d_asset_resolver_mount_memory_pack()`.
The embedded bytes are generated from the same pack writer as the on-disk
`.sdl3dpak` output, so they inherit the same compression and obfuscation
settings.

For native build-time `.sdl3dpak` files:

```cmake
sdl3d_add_asset_pack(my_asset_pack
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    OUTPUT "${CMAKE_BINARY_DIR}/my_game.sdl3dpak"
    FILES
        game.game.json
        scripts/rules.lua)
```

## Fused Executables

`sdl3d_bundle` creates a Love2D-style fused executable after the generic runner
has already been built. It copies `sdl3d_runner`, packs game data with the same
`.sdl3dpak` writer used by the rest of the engine, appends those pack bytes to
the copied executable, and writes a small footer containing the pack byte range
and default game-data asset path.

```sh
build/debug/sdl3d_bundle \
  --runner build/debug/sdl3d_runner \
  --root path/to/game/data \
  --data asset://game.game.json \
  --output build/MyGame

build/MyGame
```

When a runner starts without `--root`, `--pack`, or `--embedded`, it checks its
own executable for this footer. If present, it mounts the appended pack and uses
the footer's `--data` value as the default entry point. Explicit mount flags
still take precedence, so the same binary can be run against loose data during
debugging.

If no `--file` flags are passed, `sdl3d_bundle` includes every regular file
under `--root` recursively. Pass repeated `--file <relative-path>` values to
create a smaller fused executable from an explicit manifest.

For Emscripten tests or demos that use loose source directories:

```cmake
sdl3d_target_preload_asset_directory(my_target "${CMAKE_CURRENT_SOURCE_DIR}/data" "/data")
```

## Sprite Runtime Loading

`sdl3d_game_data_load_sprite_asset()` turns an authored sprite descriptor into
runtime-owned billboard textures and rotation sets. The loader accepts either a
sprite sheet description from `assets.sprites` or a file-list source used by
generalized game code, then decodes the source once and keeps the resulting
textures until `sdl3d_sprite_asset_free()`.

Prefer a sheet when many frames share metadata. The loader slices a sheet in
memory after one decode, which is cheaper than reading many tiny files. Use the
file-list source only when the authored asset layout genuinely does not fit a
sheet.

Sprite assets may author an overlay effect, currently `melt`, plus timing
fields that tell the presentation layer when the effect starts and how long it
runs.

Sprite assets can also author optional `shader_vertex_path` and
`shader_fragment_path` entries. The loader reads shader text once and keeps it
with the runtime sprite. The GL backend compiles each distinct shader source
pair on first use and reuses the resulting program for later draws. The
software backend ignores authored shader source and renders through the normal
sprite or overlay path.

UI image assets may also be backed by a sprite asset id. The presentation layer
loads the sprite through the same runtime helper and caches the resulting
texture for UI drawing, keeping 2D art on the shared sprite path.
