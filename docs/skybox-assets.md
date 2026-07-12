# Skybox Asset Contract

Slayer3D uses a sky-sphere panorama as the default sky asset format for editor
and runner-authored maps.

## Directory Layout

Project media should place sky presets under `media/skyboxes/`:

```text
media/
  skyboxes/
    afternoon/
      sphere.png
    midnight/
      sphere.png
```

Each immediate or nested skybox directory is treated as one selectable skybox
when it contains `sphere.png`.

## Required File

`sphere.png` is the authoritative sky texture for a skybox preset.

- It must be an equirectangular panorama.
- It must use a 2:1 aspect ratio.
- The horizontal axis must wrap seamlessly.
- The top and bottom should tolerate clamped sampling.
- Prefer sky-only side coverage; avoid cubemap-style 50/50 sky and empty
  ground splits unless that horizon is intentional.

The built-in presets use `2048x1024` PNG files. Larger panoramas are valid as
long as the target platform can afford the texture memory.

## Map References

Saved maps should reference sky spheres with project-relative paths:

```json
{
  "skybox": {
    "mode": "sphere",
    "preset": "afternoon",
    "sphere": "skyboxes/afternoon/sphere.png"
  }
}
```

Absolute host paths are not portable and should fail validation. Preset-only
records resolve to `skyboxes/<preset>/sphere.png`.

## Legacy Compatibility

Older content may still use six-face cubemap files:

```text
px.png nx.png py.png ny.png pz.png nz.png
```

or Quake-style animated layer files:

```text
layer_outer.png
layer_inner.png
```

These formats remain readable for compatibility. New editor-authored defaults
should use `sphere.png`; when a directory contains `sphere.png`, that panorama
is preferred over legacy cubemap or layer files.

## Authoring Guidance

Sky spheres are suitable for normal skies, abstract skies, underwater domes,
space scenes, and horizon-based cityscapes. A cityscape should be authored as a
panorama with skyline detail around the lower horizon band and open sky above.
The current renderer treats it as distant background art, not as geometry with
collision or parallax-correct nearby buildings.
