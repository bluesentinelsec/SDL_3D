# Logging Strategy

SDL3D uses SDL's logging system directly.

- SDL3D logs through a dedicated custom SDL log category.
- Callers control verbosity with SDL log priorities.
- Callers may install their own SDL log output callback with `SDL_SetLogOutputFunction()`.
- SDL3D does not introduce a parallel logging abstraction.

Recommended usage:

- Set the SDL3D category priority with `sdl3d_set_log_priority()` or directly with `SDL_SetLogPriority(sdl3d_log_category(), ...)`.
- Route output globally through SDL if your application needs custom sinks, formatting, or integration with another logging backend.
- Keep messages single-line and structured enough to be useful in CI logs.
