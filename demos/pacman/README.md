# SDL3D Pac-Man Demo

This is a data-authored maze chase proof of concept. It uses JSON for reusable
engine primitives and Lua for Pac-Man-specific rules:

- `grid_maps` define the maze topology.
- `motion.grid_agent` keeps Pac-Man and ghosts moving cell-to-cell.
- `grid.spawn_from_glyphs` creates walls, pellets, and power pellets from the
  map.
- Actor pools handle collectible/effect lifecycle.
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

Controls: arrow keys or WASD move, Enter pauses/selects, Escape backs out.
