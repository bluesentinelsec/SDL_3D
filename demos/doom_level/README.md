# Doom Level

First-person walkthrough of a Doom E1M1-inspired level built from
sector definitions. Tests the sector-based level builder for watertight
mesh generation, authored sector doors, sector hazard sensors, and
data-authored props such as robot sprites, health pickups, and crates.

6 connected rooms: start room, corridor, nukage room, side passage,
outdoor area, exit room.

## Run

The Doom level is now a data-authored demo launched by the generic runner:

```sh
./build/debug/sdl3d_runner --root demos/doom_level/data --data asset://doom_level.game.json
```

To start directly in the play scene while iterating:

```sh
./build/debug/sdl3d_runner \
  --root demos/doom_level/data \
  --data asset://doom_level.game.json \
  --scene scene.doom_level.play
```

The old native Doom host sources remain in this directory temporarily as a
parity reference while the data-driven migration is completed, but they are no
longer built as a demo target.

## Controls

- **WASD** — Move
- **Mouse** — Look
- **P** — Pause
- **E** — Interact
- **F** or left mouse — Fire
- **L** — Toggle lighting
- **M** — Cycle sector render variant
- **Tab** — Cycle render profile
- **Escape** — Quit
