# Generic Game Data Model

Slayer 3D's data model separates reusable engine concepts from game-specific
rules. The same core should support fixed-screen arcade games, sprite-heavy 2D
games, platformers, RPGs, sector/portal worlds, brush-style worlds, and general
3D scenes.

## Core Principles

- **Generic core, optional modules.** The core cannot assume a genre, world
  type, score model, health model, inventory, weapon system, or player camera.
- **Data-first composition.** Authored JSON should connect reusable primitives
  before a game reaches for Lua or native code.
- **Game-specific policy in Lua.** Lua owns rules and calculations that are not
  reusable engine behavior.
- **Reusable systems in C.** If several games need the same capability, expose
  it as an engine primitive and make it authorable from JSON/Lua.
- **Deterministic validation.** Bad references, duplicate names, invalid
  payloads, and unsupported schemas should fail before gameplay starts.

## Runtime Layers

1. Platform loop: SDL windows, timing, events, input snapshots, renderer
   context, audio frame updates, and shutdown.
2. Core state: actors, properties, signals, timers, storage, assets, and logic
   dispatch.
3. World/spatial modules: fixed screen, tile grid, room graph, sector/portal,
   brush world, or general 3D scene.
4. Simulation modules: movement, collision, particles, cameras, audio emitters,
   haptics, networking, persistence, and runtime actor pools.
5. Data runtime: JSON/Lua loading, scene update/render, input profile refresh,
   managed network orchestration, and presentation caches.
6. Generic runner: process-level executable that mounts assets and advances the
   data runtime without knowing the game.

Lower layers should not depend on higher layers.

## Entities And Actors

An actor is a named runtime object with:

- stable authored name
- active state
- tags
- transform
- typed properties
- components

Actors should describe engine-owned facts. Game rules should avoid encoding
state in names when tags or properties communicate the intent more clearly.

## Components

Components attach reusable behavior or presentation to an actor. Examples:

- render primitives, sprites, billboards, models, lights, and particles
- collision shapes and bounds sensors
- input/control components
- script-backed controllers
- audio emitters

Components should stay small and explicit. If a component starts encoding a
whole game mode, the behavior likely belongs in JSON logic, Lua, or a new
engine system.

## Logic

Slayer 3D logic is signal-oriented:

- sensors observe state and emit signals
- timers emit signals after delays or intervals
- signal bindings execute ordered action arrays
- conditions gate actions, UI, updates, and scene behavior

Generic actions should cover common operations such as property changes,
signals, timers, scene transitions, persistence, actor spawn/despawn, network
session work, and input profile application. Lua adapters should fill only the
gaps that are truly game-specific.

## World Models

A world is a spatial context. The root model should not assume one spatial
strategy. Supported and planned world families include:

- fixed 2D or 2.5D playfields
- tile maps and grid worlds
- room/screen graphs
- sector/portal maps
- brush-style worlds
- general 3D scenes
- mixed worlds

World-specific systems should be optional modules. A sector renderer should not
leak sector assumptions into a fixed-screen game; a tile-map system should not
force grid assumptions into a 3D scene.

Editor and tooling code should prefer the generic world-model query layer over
parsing authored scene JSON directly. The game-data runtime can enumerate active
world instances, return world-space bounds, perform generic trace and
point-contents queries, and expose diagnostics with stable references back to
the underlying sector level or brush world. Sector and brush implementations
remain distinct internally, but tools can start from the shared
`slayer3d_game_data_for_each_world_model_instance`,
`slayer3d_game_data_trace_world_models`, and
`slayer3d_game_data_query_world_model_point` APIs.

## Data Boundary

Use JSON for:

- app, window, renderer, storage, and audio defaults
- scenes, transitions, menus, widgets, and dynamic lists
- actors, components, cameras, lights, sprites, materials, and effects
- input actions, device assignments, and input profiles
- sensors, timers, signals, conditions, and action arrays
- persistence and standard options
- network protocol, replication schemas, control messages, runtime bindings,
  and session-flow events

Use Lua for:

- game-specific movement, collision, scoring, spawning, or AI policy
- rules that are too specialized to justify an engine primitive
- game-specific save-data interpretation

Use C for:

- reusable engine systems
- platform integration
- renderer/world backends
- asset loaders and pack primitives
- high-performance primitives that should be available to future games

## Promotion Rule

When the same Lua or host-code pattern appears in multiple games, consider
promoting it into an engine primitive. Once promoted, document it, validate it,
test it, and consume it through JSON/Lua rather than repeating native glue.
