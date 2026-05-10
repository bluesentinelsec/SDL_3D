# Doom Level

First-person walkthrough of a Doom E1M1-inspired level built from
sector definitions. Tests the sector-based level builder for watertight
mesh generation, authored sector doors, sector hazard sensors, and
data-authored props such as animated model actors, robot sprites, health
pickups, and crates.

6 connected rooms: start room, corridor, nukage room, side passage,
outdoor area, exit room.

## Run

The Doom level is a data-authored demo launched by the generic runner. It does
not have a demo-specific C host; gameplay, presentation, controls, interactions,
and scene flow are authored in JSON and Lua.

```sh
./build/debug/slayer3d_runner --root demos/doom_level/data --data asset://doom_level.game.json
```

To start directly in the play scene while iterating:

```sh
./build/debug/slayer3d_runner \
  --root demos/doom_level/data \
  --data asset://doom_level.game.json \
  --scene scene.doom_level.play
```

## Controls

- **WASD** — Move
- **Mouse** — Look
- **P** — Pause
- **E** — Interact
- **F** or left mouse — Fire
- **L** — Toggle lighting
- **M** — Cycle sector render variant
- **Tab** — Cycle render profile
- **1** — Modern render profile
- **2** — PS1 render profile
- **3** — N64 render profile
- **4** — DOS render profile
- **5** — SNES render profile
- **6** — Grayscale render profile
- **7** — Game Boy render profile
- **Escape** — Quit
