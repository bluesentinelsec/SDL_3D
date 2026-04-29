# SDL3D Storage

`sdl3d_storage` is the writable companion to the read-only asset resolver.
Shipped files should continue to use `asset://`; persistent and generated files
should use storage paths:

- `user://` for player-owned data: settings, saves, high scores, profiles, replays
- `cache://` for disposable generated data: materialized audio, shader caches, thumbnails

The storage root is derived from stable metadata:

```json
{
  "storage": {
    "organization": "Blue Sentinel Security",
    "application": "SDL3D Pong",
    "profile": "default"
  }
}
```

Game-data runtimes expose this authored block through
`sdl3d_game_data_get_storage_config()`, so a managed app can create storage from
the same data file that declares its window, assets, scenes, and gameplay.

The organization and application identifiers are part of the root path on every
SDL3D platform policy. This intentionally differs from some older PhysicsFS
Unix examples that used only the application name; keeping both identifiers
reduces collisions and matches the Windows/macOS convention.

## Platform Policy

SDL3D uses `SDL_GetPrefPath(organization, application)` for native runtime
storage. The deterministic planner used by tests and tools follows this shape:

| Platform policy | Example user root |
| --- | --- |
| Windows | `%APPDATA%/Blue Sentinel Security/SDL3D Pong` |
| Apple | `Application Support/Blue Sentinel Security/SDL3D Pong` |
| Unix/Linux | `$XDG_DATA_HOME/Blue Sentinel Security/SDL3D Pong` |
| Android | app-private root plus `Blue Sentinel Security/SDL3D Pong` |
| Emscripten | persistent virtual root plus `Blue Sentinel Security/SDL3D Pong` |

If a profile is configured, SDL3D appends `profiles/<profile>`. The cache root
then appends `cache`.

Game-data audio actions materialize resolver-backed audio assets into
`cache://audio`. Hosts that need to preload or inspect the resolved path can use
`sdl3d_game_data_prepare_audio_file()`, which applies the same cache policy as
`audio.play_sfx` and `audio.play_music`.

## Safety Rules

Storage virtual paths are intentionally strict:

- accepted schemes are only `user://` and `cache://`
- absolute paths are rejected
- `..`, `.`, empty path segments, backslashes, and additional URI schemes are rejected
- writes create parent directories automatically
- writes use a temporary file in the target directory and rename it into place

This keeps user-writable data separate from shipped assets while leaving room for
future explicit override, patch, cloud sync, or mod systems.
