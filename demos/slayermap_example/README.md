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

The example prints high-level counts through the public `slayer3d_map_*` API and
also reports the playable scene descriptor: box brush count, actor count, and
the first actor/object marked with `properties.type = "player-character"`.

Current editor-to-engine workflow:

1. Create and texture a brush map in the editor.
2. Place a capsule from `Things > Objects`.
3. Add Data properties `type = player-character` on that capsule, or use the
   editor player-start flow which exports that property automatically.
4. Use `File > Validate Document` to check the map.
5. Use `File > Save Document` or `Save Document As...` to write a
   `.slayermap.json` file.
6. Run this example against the saved map to prove the engine library can load
   the map and resolve the player spawn.
7. Run the map through the generic runner to materialize and play the generated
   first-person brush scene:

```sh
./build/debug/slayer3d_runner --map /tmp/level.slayermap.json
```

The playable runner bridge is intentionally conservative: it constructs
brush-collision/render geometry, spawns the first `player-character` as a
first-person controller, and preserves material colors. Project-specific actor
behavior, lights/effects, and generic connections remain data for callers to
interpret as game systems are built out.
