# SlayerMap Example

This example is a small data-authored level package for the standalone
`.slayermap.json` format. It is intentionally focused on game-facing map data:
brushes, materials, actor instances, prefabs, lights, effects, skybox selection,
and generic event/action connections.

The map lives at:

```text
demos/slayermap_example/maps/training_room.slayermap.json
```

Build demos with:

```sh
cmake -S . -B build -DSLAYER3D_BUILD_DEMOS=ON
cmake --build build --target slayer3d_slayermap_example
```

Run the loader example with no arguments to load the bundled training room, or
pass a path to any other `.slayermap.json` file:

```sh
./build/demos/slayermap_example/slayer3d_slayermap_example
./build/demos/slayermap_example/slayer3d_slayermap_example path/to/level.slayermap.json
```

The example prints high-level counts through the public `slayer3d_map_*` API.
Game runtimes can layer their own materialization logic on top: spawn actors,
construct brush collision, bind lights/effects, and interpret generic
connections according to project-specific gameplay rules.
