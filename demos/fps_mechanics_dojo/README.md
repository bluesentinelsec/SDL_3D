# FPS Mechanics Dojo

This is a data-only testbed for reusable FPS world mechanics. It intentionally
uses mock geometry, authored lights, and simple trigger volumes instead of
finished media assets.

Run it with the generic runner:

```sh
./build/debug/sdl3d_runner --root demos/fps_mechanics_dojo/data --data asset://fps_mechanics_dojo.game.json
```

The first scene demonstrates existing data-authored primitives:

- `controller.fps_sector` movement
- `controller.fps_sector.launch` for jump-pad style movement
- `controller.fps_sector.teleport` for teleport pads
- `sensor.volume` and `sensor.sector`
- `sector_doors` and `sector_platforms`
- actor archetype/instance mockups with optional `editor` metadata

The dojo is meant to grow alongside reusable mechanics from issue #282.
