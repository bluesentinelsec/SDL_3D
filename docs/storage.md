# Slayer 3D Storage

`slayer3d_storage` is the writable companion to the read-only asset resolver.
Shipped files should continue to use `asset://`; persistent and generated files
should use storage paths:

- `user://` for player-owned data: settings, saves, high scores, profiles, replays
- `cache://` for disposable generated data: materialized audio, shader caches, thumbnails

The storage root is derived from stable metadata:

```json
{
  "storage": {
    "organization": "Example Studio",
    "application": "Example Game",
    "profile": "default"
  }
}
```

Game-data runtimes expose this authored block through
`slayer3d_game_data_get_storage_config()`, so a managed app can create storage from
the same data file that declares its window, assets, scenes, and gameplay.

The organization and application identifiers are part of the root path on every
Slayer 3D platform policy. This intentionally differs from some older PhysicsFS
Unix examples that used only the application name; keeping both identifiers
reduces collisions and matches the Windows/macOS convention.

## Platform Policy

Slayer 3D uses `SDL_GetPrefPath(organization, application)` for native runtime
storage. The deterministic planner used by tests and tools follows this shape:

| Platform policy | Example user root |
| --- | --- |
| Windows | `%APPDATA%/Example Studio/Example Game` |
| Apple | `Application Support/Example Studio/Example Game` |
| Unix/Linux | `$XDG_DATA_HOME/Example Studio/Example Game` |
| Android | app-private root plus `Example Studio/Example Game` |
| Emscripten | persistent virtual root plus `Example Studio/Example Game` |

If a profile is configured, Slayer 3D appends `profiles/<profile>`. The cache root
then appends `cache`.

Game-data audio actions materialize resolver-backed audio assets into
`cache://audio`. Hosts that need to preload or inspect the resolved path can use
`slayer3d_game_data_prepare_audio_file()`, which applies the same cache policy as
`audio.play_sfx` and `audio.play_music`.

Lua game scripts can use the same safe roots through the gameplay API:

```lua
local ok, err = slayer3d.storage.write("user://settings/options.json", json_text)
local text, read_err = slayer3d.storage.read("user://settings/options.json")
local exists = slayer3d.storage.exists("cache://generated/status.txt")
```

See [docs/game-data-lua.md](game-data-lua.md) for the full Lua helper reference, including the storage return contract and error behavior.

Scripts can pair storage with the gameplay JSON helpers when persisting
structured settings, saves, scores, or generated cache metadata:

```lua
local options = slayer3d.json.decode(ctx.storage.read("user://settings/options.json") or "{}")
options.fullscreen = true
ctx.storage.write("user://settings/options.json", slayer3d.json.encode(options))
```

The adapter context exposes the same table as `ctx.storage`. These bindings use
the storage API's normal validation, so scripts cannot escape through absolute
paths, `..`, empty segments, backslashes, or unknown schemes.

## Safety Rules

Storage virtual paths are intentionally strict:

- accepted schemes are only `user://` and `cache://`
- absolute paths are rejected
- `..`, `.`, empty path segments, backslashes, and additional URI schemes are rejected
- writes create parent directories automatically
- writes use a temporary file in the target directory and rename it into place

This keeps user-writable data separate from shipped assets while leaving room for
future explicit override, patch, cloud sync, or mod systems.
