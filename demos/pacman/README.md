# SDL3D Pac-Man Demo

This is a data-authored maze chase proof of concept. It uses JSON for reusable
engine primitives and Lua for Pac-Man-specific rules:

- `grid_maps` define the maze topology.
- `motion.grid_agent` keeps Pac-Man and ghosts moving cell-to-cell.
- `grid.spawn_runs_from_glyphs` batches wall runs from the map.
- `grid_pickup_layers` render and collect pellets from a compact grid layer.
- Actor pools handle burst/effect lifecycle.
- Lua handles score, pellet collection, power mode, ghost patrol, and ghost
  respawn behavior.

Run it with the generic runner:

```sh
./build/release/sdl3d_runner --root demos/pacman/data --data asset://pacman.game.json
```

For play-scene iteration:

```sh
./build/release/sdl3d_runner --root demos/pacman/data --data asset://pacman.game.json --scene scene.play
```

Controls: arrow keys or WASD move, B toggles the first-person camera, Enter
pauses/selects, Escape backs out.
