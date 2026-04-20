# Proposed GitHub Issue

## Title

Renderer Architecture Roadmap: backend-agnostic 3D API, OpenGL parity, and profile support

## Body

We should formalize the renderer architecture before adding more backend-specific behavior.

The target is:

- one high-level 3D API powered by SDL
- multiple backends behind that API
- software renderer as fallback/reference implementation
- OpenGL as the first hardware backend
- `SDL_GPU` as a later hardware backend
- renderer-independent profiles for visual aesthetics: `modern`, `PS1`, `N64`, `DOS`, `SNES`

### Motivation

The recent OpenGL work proved that the hardware path is viable, but it also exposed an architectural gap:

- backend logic and profile logic are still partially entangled
- software and OpenGL parity is incomplete
- logical-resolution presentation needs a more general approach
- some GL behavior is still immediate-mode-style and not game-ready

We should fix that now, while the backend surface area is still manageable.

### Goals

1. Keep the public API high-level and backend-agnostic.
2. Treat software as a correctness/reference backend, not just a fallback.
3. Keep OpenGL as the first production hardware backend.
4. Add `SDL_GPU` later without redesigning the public API.
5. Make render profiles apply consistently across all backends.
6. Support logical resolution / virtual resolution correctly on every backend.

### Proposed architecture

#### 1. Separate backend choice from render profile choice

Backends:

- `software`
- `opengl`
- `sdl_gpu` (future)

Profiles:

- `modern`
- `PS1`
- `N64`
- `DOS`
- `SNES`

Profiles should be policy bundles, not backend types. They should control things like:

- shading mode
- fog mode/evaluation
- tone mapping
- texture filtering
- affine vs perspective UVs
- vertex snapping
- color quantization
- dithering

That should allow combinations like:

- `software + PS1`
- `opengl + modern`
- `sdl_gpu + N64`

#### 2. Define an internal backend interface in renderer terms

We should introduce a clear internal renderer backend contract for things like:

- backend/context creation and destruction
- logical-size render target creation
- mesh creation/update/destruction
- texture creation/update/destruction
- per-frame begin/end
- draw submission
- present

This should live below the public `sdl3d_*` API and above each backend implementation.

#### 3. Render all backends to a logical-resolution offscreen target first

This is the correct long-term solution for virtual resolution and resize-independent presentation.

Instead of relying on window viewport tricks alone, every backend should:

- render to a target at logical resolution
- present/blit/upscale that target to the actual window size according to presentation mode

This should make logical size handling consistent across software, OpenGL, and future `SDL_GPU`.

#### 4. Treat the software renderer as the reference implementation

The software backend should define expected behavior for:

- lighting
- fog
- tonemapping
- texture sampling
- skinning
- profile behavior

Hardware backends should be validated against that behavior using tests.

### OpenGL roadmap

OpenGL is the first production hardware backend, so it should be upgraded from proof-of-concept to retained renderer.

#### Phase 1: correctness and parity

- finish feature parity with software for:
  - textures
  - animation/skinning
  - lighting
  - fog
  - tonemapping
  - profile behavior
- remove remaining “special case” GL paths that bypass renderer policy
- add image-based parity tests against software

#### Phase 2: retained GPU resources

- upload meshes once and reuse GPU buffers
- upload textures once and reuse GPU textures/samplers
- introduce GPU-side mesh/material/texture caches
- remove per-draw buffer/object churn

#### Phase 3: scene-quality improvements

- proper GPU skinning
- shadow maps
- normal maps / more complete material support
- better light/material handling
- profile-specific shader/pipeline variants where needed

### `SDL_GPU` roadmap

`SDL_GPU` should be added only after the backend boundary is clean.

The `SDL_GPU` backend should implement the same internal backend contract as OpenGL and software, not introduce a new public API shape.

This should give us:

- SDL-owned GPU abstraction
- easier portability
- backend consistency at the API layer

### Profiles roadmap

Profiles should become first-class shared renderer descriptors, not scattered backend-specific toggles.

Each profile should explicitly define:

- shading model
- texture filtering rules
- UV interpolation mode
- fog behavior
- quantization/dithering rules
- snapping rules
- presentation/post-processing expectations

Backends then implement those semantics in the most appropriate way.

### Suggested implementation order

1. Define internal backend interface.
2. Introduce backend-independent profile descriptor/application path.
3. Move all backends to logical-resolution offscreen rendering.
4. Finish OpenGL correctness parity with software.
5. Convert OpenGL to retained GPU resources.
6. Add renderer parity tests.
7. Begin `SDL_GPU` backend implementation.

### Acceptance criteria

- public API does not expose backend-specific concepts
- backend and profile are independently selectable
- software and OpenGL produce comparable output for core features
- logical resolution works consistently across resize/presentation modes
- OpenGL path is correct enough to serve as first-class hardware backend
- architecture is ready for `SDL_GPU` without public API redesign
