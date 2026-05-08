# SDL3D ArmaLite PoC

Runner-first horizontal side-scroller shoot 'em up proof of concept.

This demo is intentionally authored as JSON and Lua data. It uses the generic
`sdl3d_runner`, actor pools, dynamic lighting, emissive projectiles, particles,
wave schedules, collision actions, scrolling parallax geometry, and Lua
gameplay rules. It does not have a game-specific native host.

Run from the repository root:

```sh
./build/default/sdl3d_runner --root demos/armalite/data --data asset://armalite.game.json
```

Jump directly into gameplay:

```sh
./build/default/sdl3d_runner --root demos/armalite/data --data asset://armalite.game.json --scene scene.play
```

Controls:

- Player 1: WASD or arrow keys to move, Space to fire
- Player 2 local co-op: IJKL to move, Right Shift to fire
- Enter or P: pause
- Backspace: quit

Data-driven proof points:

- `projectile.fire` handles pooled player projectile spawning and cooldowns.
- `logic.wave_schedules` spawn enemies, asteroids, and enemy fire without Lua
  spawn loops.
- `collision.on_overlap` handles projectile/threat/player overlaps with JSON
  actions.
- `motion.scroll_wrap` drives parallax background strips.
- Pooled projectile and explosion archetypes carry their own light/effect
  components.
