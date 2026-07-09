# Slayer3D Editor Media Directory

The Slayer3D editor uses one project media directory for designer-provided assets.
This keeps texture, model, skybox, liquid, sprite, effect, audio, and font discovery
predictable across the editor, saved maps, and runner workflows.

## User Experience

On startup, the editor resolves media in this order:

1. `--media-path <dir>` when provided.
2. A `media` directory in the current working directory.
3. The opened editor project manifest `media_root`, when explicitly provided
   for compatibility with project-authored launch workflows.

If no usable media directory is found, editor tools remain available with clear
missing-source warnings. Designers can choose or reload the media directory from
`File > Settings`.

## Expected Layout

```text
media/
  textures/
  models/
  skyboxes/
  liquids/
  sprites/
  fonts/
  audio/
  effects/
```

Tools read from their corresponding subdirectories. For example, the texture
tool scans `media/textures`, the Things actor browser scans `media/models`, and
the liquid tool uses `media/liquids`.

The editor recursively scans supported asset directories, so designers can use
subdirectories to organize assets.

## Maps

Saved Slayer3D maps should reference project assets with relative paths so a
project directory can be shared across machines. Absolute local media paths are
session configuration and should not be authored into portable map data.

## Embedded Assets

Project media should not be hidden behind editor binary embedding. Editor-owned
fallback assets, icons, and bundled fonts are allowed, but project textures,
models, skyboxes, liquids, sprites, audio, and effects should come from the
selected media directory.
