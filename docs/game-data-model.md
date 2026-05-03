# SDL3D Generic Game Data Model

This document defines the engine-level data model boundary for issue #120. The goal is to keep SDL3D open to many game families while preserving the systems that already work for the Doom-level demo.

The engine should provide reusable primitives for simulation, input, entity state, rendering, audio, collision, triggers, logic, assets, and persistence. Individual games should compose those primitives through data and optional adapters. A Doom-like FPS is one supported profile, not the shape of the whole engine.

## Design Principles

- **Generic core, optional modules.** The core cannot assume sectors, first-person movement, HP, score, inventory, weapons, or any specific genre rule. Those belong in optional modules or game-specific adapters.
- **Single ownership.** Each subsystem owns one category of state. Logic coordinates systems but does not own render assets, player physics, audio playback internals, or game-specific rule state.
- **Open extension.** New game types should add modules, components, actions, sensors, or adapters without rewriting existing demos.
- **Low-level and high-level use.** Callers may use the primitive C APIs directly, while demos and future tools can use higher-level authoring data.
- **Deterministic composition.** Logic dispatch, timers, simulation updates, and action order should be predictable and testable.
- **Regression-resistant demos.** Each demo is a compatibility test for a genre profile. New work should not break older demos.

## Runtime Layers

SDL3D should be organized as a stack of small layers. Lower layers should not depend on higher layers.

1. **Platform and loop layer**
   - Owns SDL initialization, windows, render contexts, timing, event polling, input updates, and optional audio creation.
   - Current API: `sdl3d_run_game`, `sdl3d_game_context`.
   - This layer should stay genre-neutral.

2. **Core state layer**
   - Owns identity, properties, signals, timers, and generic logic.
   - Current APIs: `sdl3d_actor_registry`, `sdl3d_properties`, `sdl3d_signal_bus`, `sdl3d_timer_pool`, `sdl3d_logic_world`.
   - This is the minimum shared model for Pong and Doom.

3. **World and spatial layer**
   - Describes where entities live and how spatial queries work.
   - This may be a fixed playfield, tile map, room graph, sector map, or 3D scene.
   - Sectors are optional and belong to the FPS/sector profile.

4. **Simulation modules**
   - Movement, collision, doors, platforms, projectiles, AI controllers, particles, audio emitters, cameras, and other typed systems.
   - Modules may be reusable across genres, but they should not become mandatory core dependencies unless every game type needs them.

5. **Logic composition layer**
   - Sensors emit signals.
   - Signals execute ordered actions.
   - Logic entities such as relays, timers, counters, toggles, branches, sequences, random selectors, and once gates compose behavior.
   - Game adapters bridge actions to state the engine should not own.

6. **Data authoring layer**
   - Future JSON-family files and packed archives should describe entities, components, sensors, signals, actions, assets, and profile choices.
   - Runtime code should validate data before gameplay starts.

## World Model

A world is a spatial context. It is not always a Doom-style level.

Supported world forms should include:

- fixed 2D or 2.5D playfield
- grid or tile map
- room or screen graph
- sector/portal map
- 3D scene
- mixed worlds, such as a 3D game with an arcade minigame

The generic world model should expose:

- stable world id/name
- coordinate convention and units
- optional bounds
- optional gravity or acceleration policy
- optional collision space
- optional zones, rooms, screens, sectors, or portals
- optional spawn points
- optional cameras
- optional lighting and ambience

### Mandatory vs Optional World Data

Mandatory:

- identity
- update/render participation rules
- a way to attach entities or reference world-local entities

Optional:

- sectors
- tile grids
- nav graphs
- room transitions
- gravity
- collision geometry
- streaming chunks
- baked lighting data

This keeps Pong free from sector data while still letting Doom use sector floors, ceilings, portals, slopes, conveyors, damage floors, and ambient ids.

## Entity Model

An entity is any named thing that can participate in gameplay or presentation.

Examples:

- paddle
- ball
- player
- enemy
- projectile
- pickup
- door
- platform
- camera
- light
- particle emitter
- trigger volume
- ambient zone
- UI-facing score object

The existing actor registry is the closest current API. It already owns stable ids, names, active state, positions, properties, and triggers. Future entity work should evolve this model rather than duplicate it.

### Universal Entity Fields

These fields are general enough for all target game families:

- stable numeric id
- stable string name
- active/enabled state
- tags or category labels
- transform or position
- property bag
- optional components
- optional triggers/sensors

### Optional Component-Owned Fields

These should not be universal:

- sector id
- sprite animation state
- model mesh/material state
- collision shape
- HP or damage
- inventory
- weapon state
- AI state
- audio emitter state
- particle emitter state
- save-state policy

Those fields belong to components, modules, or game-specific adapters.

## Component and Module Model

SDL3D does not need to become a strict ECS before it can be generic. A practical model is:

- entity registry for identity and shared state
- typed systems for performance-sensitive or complex behavior
- component-like authored data that tells systems which entities they own
- game adapters where rules are too game-specific

Useful reusable modules:

- transform/state
- renderable sprite, billboard, model, mesh, tile, debug primitive
- authored sprite asset metadata for sheets, directional frames, and lighting-aware 2D-in-3D sprites
- collider or trigger volume
- movement controller
- input controller
- animation state
- audio emitter
- particle/effect emitter
- camera
- light
- logic bindings
- save-state hooks

Optional genre modules:

- FPS mover
- sector/portal runtime
- tile-grid mover
- platformer mover
- top-down mover
- arcade paddle/ball helpers
- formation/wave controller
- pathfinding
- inventory or equipment
- combat/HP

## Logic Model

The logic layer is the data-composition model for gameplay. It should not become the only game data model.

Logic owns:

- signal-to-action bindings
- deterministic action order
- target references and target resolution
- sensor edge state
- small logic entity state
- timer-driven composition

Logic does not own:

- player movement internals
- rendering resources
- audio engine internals
- door mesh drawing
- sector mesh rebuild policy
- game-specific HP, inventory, score, or combat formulas

Those are reached through direct engine actions or narrow game adapters.

### Logic Primitive Vocabulary

Sensors:

- contact volume
- zone/sector enter, exit, and level
- proximity
- collision
- use/interact
- shot/hit
- property change
- timer

Actions:

- set/toggle/add property
- emit signal
- start/cancel timer
- enable/disable entity
- move/teleport entity
- apply impulse/launch
- door command
- camera command
- audio command
- particle/effect command
- world/sector mutation
- spawn/despawn
- game-adapter callback

Logic entities:

- relay
- timer
- counter
- toggle
- branch
- sequence
- random selector
- once gate
- state machine

## Game-Specific Adapters

Adapters are explicit extension points for behavior the generic engine should not hardcode.

Good adapter candidates:

- move or teleport the game-specific player object
- set the active gameplay camera
- apply HP damage according to a game's combat rules
- update score/lives according to a game's rules
- spawn a game-specific actor archetype
- command a game-owned effect bundle
- resolve a game-specific inventory, quest, or dialog state

Bad adapter candidates:

- generic property mutation
- generic signal emission
- timer start/stop
- contact sensor evaluation
- basic AABB/sphere collision
- input action lookup

If a feature is useful to Pong, Doom, and a platformer with the same semantics, it should usually be an engine primitive. If the semantics differ by game, keep it behind an adapter or optional module.

## Naming and Identity

Future data files should use stable names and ids. Names should be human-readable and namespaced by role.

Recommended patterns:

- `entity.player`
- `entity.ball`
- `entity.paddle.left`
- `sector.nukage_basin`
- `door.nukage.north`
- `camera.nukage_surveillance`
- `effect.nukage_particles`
- `sound.ambient.nukage`
- `signal.score.left`
- `signal.teleport.dragon_enter`
- `logic.nukage_alarm.sequence`

Rules:

- Numeric ids are runtime handles and may be faster.
- String names are authored handles and must be stable across data reloads.
- Data loaders should reject duplicate names in the same namespace.
- Cross-file references should use asset ids or names, not raw paths, once an asset resolver exists.
- Signal dispatch can use numeric ids internally, but authored data should allow named signals.

## Assets and Resources

The data model should distinguish asset identity from file location.

Authoring data should prefer:

- `texture.door.hatch`
- `sprite.robot.walk`
- `sound.weapon.fire`
- `music.level.nukage`
- `material.wall.industrial`

The asset system should resolve these ids to paths or archive entries. This supports packed archives, platform-specific assets, and future hot reload.

Short-term demos may still reference paths directly where no resolver exists, but new generic APIs should not require raw file paths in gameplay data.

## Input Model

Input is already action-oriented and should stay generic.

Generic concepts:

- actions
- analog axes
- device bindings
- keyboard, mouse, and gamepad mappings
- contexts such as gameplay, menu, debug/editor, cutscene
- recording/playback

Rules:

- Demos should use the input API for gameplay, not raw SDL events, except where handling platform/window events is appropriate.
- "use/interact" should be a first-class gameplay action.
- Game-specific demos decide what actions mean.
- Local multiplayer should eventually map actions to player slots or control contexts.

## Collision and Movement

Collision should be separate from visual geometry. Render meshes may help derive collision in tools, but runtime gameplay should use explicit collision data.

Shared collision primitives:

- AABB
- sphere/circle
- ray
- triangle query
- trigger-only volume

Optional collision modules:

- tile collision
- grid occupancy
- capsule/cylinder collision
- sector line/portal collision
- slope and stair handling
- moving platform dynamic obstacles
- one-way platforms

Movement controllers should be policy-based:

- fixed-screen arcade mover
- grid mover
- top-down mover
- platformer mover
- FPS mover
- third-person mover
- flying or rail shooter mover

The FPS mover is a strong module for Doom, but it should not be the base movement abstraction for other genres.

## Rendering and Presentation

Rendering should support both simple and advanced demos with the same engine.

Renderable categories:

- 2D sprite
- billboard sprite
- animated sprite
- authored sprite asset metadata
- tile map
- 3D model
- generated primitives
- sector/portal geometry
- particles
- UI/HUD
- debug draw

Presentation state:

- material
- texture or sprite asset
- animation clip
- facing rule
- lighting participation
- camera participation
- render profile
- screen-space feedback

Lighting is part of the engine's generality story. Even Pong should be allowed to look good through 3D presentation, materials, dynamic lighting, and simple post-processing where available.

## Audio Model

Audio should be generic and targetable by logic.

Shared concepts:

- one-shot sound effects
- loaded clips
- music streams
- ambient streams
- fade/crossfade
- named audio ids
- optional positional emitters

The existing miniaudio-backed API is a good foundation. Sector ambient ids are useful for Doom but should become one possible source of ambient changes, not the only ambient model.

## Save/Load Model

Save/load should serialize state by ownership.

Potential save categories:

- active world and profile
- entity active state, transform, tags, and properties
- spawned/despawned entities
- logic entity state
- sensor edge state when needed
- timers
- door/platform state
- player/controller state
- random seeds
- game-specific state through adapters

The engine should offer hooks and stable serialization boundaries. It should not assume every game has HP, inventory, score, sectors, or doors.

## Genre Profiles

A genre profile is a convenience bundle of modules and conventions. Profiles should not be hardcoded branches throughout the engine.

Profiles may declare required and optional modules:

| Profile | Required modules | Optional modules |
| --- | --- | --- |
| Fixed-screen arcade | entities, input, simple collision, renderables, logic | score/lives, audio, particles |
| Grid arcade | entities, grid world, input, logic | pathfinding, collectibles, score |
| Side-scroller | entities, platformer mover, collision, camera | one-way platforms, collectibles, enemies |
| Top-down adventure | entities, top-down mover, rooms/zones, interact, logic | inventory, dialog, pathfinding |
| FPS/sector action | sectors, FPS mover, collision, camera, logic | doors, lifts, teleporters, ambient zones, enemies |
| Third-person 3D | 3D collision, third-person mover, follow camera | lock-on, animation controller |
| Shooter | entities, projectile/hit logic, waves/formations | rails, powerups, score |
| Strategy/tactics | grid or nav space, selection, commands | factions, pathfinding, fog of war |

A game can mix modules from multiple profiles.

## Current System Classification

### Already Generic

- Managed game loop: useful for all demos.
- Actor registry and properties: generic identity and state.
- Signal bus and timer pool: generic event composition.
- Logic world, sensors, target refs, actions, and logic entities: generic composition.
- Input abstraction: generic device-independent controls.
- Audio engine: generic playback and ambient transitions, with sector ambience as one use case.
- Particle emitter: generic visual effect primitive.
- Core collision primitives and ray queries: generic.
- Cameras and rendering context: generic.
- Transition and UI primitives: generic presentation.

### Optional Modules That Are Already Reasonably Reusable

- Door primitive: reusable for many genres, but rendering and activation are game-owned.
- Teleporter destination payloads: reusable, though generic contact sensors plus teleport actions are preferred for authored logic.
- Sector damage, push velocity, and ambient fields: useful for FPS/sector worlds and possibly other zone-based games.
- Actor patrol controller: reusable for NPCs, but not part of core entity state.
- Sprite actor facing/animation: reusable for billboard or sprite-based games.

### FPS/Sector-Specific

- Sector/portal level builder and visibility.
- FPS mover.
- Doom-level slope/stair collision behavior.
- Sector geometry mutation as currently implemented.
- Doom-level doors placement/rendering wrapper.
- Doom-level surveillance, hazard, robot, and player modules.

These should stay optional. They should not leak assumptions into Pong, Space Invaders, Pac-Man, or side-scroller demos.

### Needs Extraction or Future Generalization

- Asset id resolver instead of path-heavy gameplay data.
- Data-driven entity/component/logic loading.
- Named signal registry for authored data.
- Generic world abstraction above fixed playfield, tile map, rooms, sectors, and 3D scenes.
- Non-FPS movement modules.
- Non-sector collision modules for arcade, grid, and platformer games.
- Save/load boundaries.
- Debug inspector for entities, properties, signals, and logic events.

## Minimum Shared Model: Pong and Doom

Pong and Doom should both be expressible with:

- managed game loop
- input actions
- actor/entity registry
- properties
- signals
- timers
- logic bindings
- render context and cameras
- audio hooks
- collision queries or contact sensors

Pong should not require:

- sectors
- portals
- FPS mover
- sector geometry mutation
- ambient sector ids
- sprite billboards
- door primitives

Doom additionally uses:

- sectors/portals
- FPS mover
- slope/stair handling
- runtime sector mutation
- doors
- conveyors/damage floors
- ambient zones
- billboard sprites
- patrol controllers
- particles and dynamic lighting

This difference is the data model boundary: Pong proves the core is not secretly FPS-specific; Doom proves optional modules can compose into a complex game.

## Pong Implementation Plan

The Pong demo should be the first proof under this model.

Goals:

- Build a fixed-screen non-sector game using SDL3D.
- Use the same managed loop, input abstraction, actor registry, properties, signals, timers, and rendering stack.
- Demonstrate simple but polished lighting and 3D presentation.
- Keep game rules small and testable.

Initial entities:

- `entity.paddle.left`
- `entity.paddle.right`
- `entity.ball`
- `entity.wall.top`
- `entity.wall.bottom`
- `entity.goal.left`
- `entity.goal.right`
- `entity.score.left`
- `entity.score.right`
- `camera.main`
- optional `light.key`, `light.rim`

Initial signals:

- `signal.game.start`
- `signal.ball.serve`
- `signal.goal.left`
- `signal.goal.right`
- `signal.score.left`
- `signal.score.right`
- `signal.round.reset`

Initial reusable primitives:

- input actions for paddle movement
- AABB or simple plane collision
- actor properties for score and velocity
- timer for serve delay
- logic actions for score increment/reset signals where existing actions allow it
- fixed camera and 3D-lit paddle/ball presentation

Pong-specific code may own:

- exact ball reflection rule
- AI or second-player policy
- score limit/win rule
- visual style

Candidate tests:

- ball collides with top/bottom bounds and reverses vertical velocity
- ball collides with paddle and reverses horizontal velocity
- entering left/right goal emits the correct scoring signal
- score increments and round reset schedules a serve
- reset returns ball and paddles to valid positions

## First Implementation Sequence

1. Land this document.
2. Implement Pong with the existing low-level primitives.
3. Extract only the reusable helpers Pong actually needs.
4. Add a second non-FPS demo that forces a different model, preferably Space Invaders or Pac-Man.
5. After at least two non-FPS demos and Doom share the same concepts, define the first JSON-family data schema.

This avoids designing an abstract schema too early while still steering the code toward data-driven authoring.

## Phase 2 Runtime Container

The first concrete API under this model is `sdl3d_game_session`.

Purpose:

- provide a small genre-neutral container for shared game services
- let demos opt into a productive default set without forcing a world type
- support borrowed services for low-level callers that want full ownership
- make future data loaders target one runtime surface instead of wiring every subsystem manually

Default owned services:

- actor registry
- signal bus
- timer pool
- logic world
- input manager

Optional or borrowed services:

- audio engine
- caller-owned world representation
- game/profile metadata

The session intentionally does not own rendering, windows, sector levels, tile maps, cameras, score, HP, inventory, or game-specific rule modules. Those remain optional systems or caller-owned state. This keeps the container useful for Pong, Doom, and future demos without turning it into a monolithic engine object.

The managed loop now creates a session internally and exposes it through
`sdl3d_game_context::session`. The context owns only loop/platform state:
window, render context, real time, pause state, and quit state. Runtime
services are accessed through `sdl3d_game_session_get_*()` calls. This is an
intentional breaking API cleanup: callers either opt into the managed loop and
use its session, or they create a session and drive their own loop.

Session update phases:

- `sdl3d_game_session_begin_frame()` updates real-time services such as audio.
- `sdl3d_game_session_update_input()` refreshes action snapshots without
  advancing simulation, which is useful while paused.
- `sdl3d_game_session_begin_tick()` refreshes input and advances timers before
  game simulation callbacks.
- `sdl3d_game_session_end_tick()` advances simulation time and tick count after
  game simulation callbacks.
- `sdl3d_game_session_tick()` is a convenience wrapper for tools or simple loops
  that do not need a callback between begin/end tick phases.
