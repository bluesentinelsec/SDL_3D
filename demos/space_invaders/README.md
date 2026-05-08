# SDL3D Space Invaders Demo

This demo is authored as runner data: JSON scenes/fragments plus Lua gameplay
rules. It intentionally starts with 3D geometry instead of sprites so the demo
can stress dynamic lighting, bloom, pooled actors, and particles.

Run from the repository root:

```sh
./build/default/sdl3d_runner --root demos/space_invaders/data --data asset://space_invaders.game.json
```

Launch directly into gameplay:

```sh
./build/default/sdl3d_runner --root demos/space_invaders/data --data asset://space_invaders.game.json --scene scene.play
```

Controls:

- Player 1: `A`/`D` or left/right arrows to move, `Space` to fire.
- Player 2 in local co-op: `J`/`L` to move, `I` to fire.
- `Enter` or `P`: pause.
- `R`: restart while playing.

Single-player and local co-op are playable. The LAN menu is present as data UI
scaffolding; a Space Invaders-specific network replication schema still needs
to be authored before LAN co-op can run through the managed network runtime.
