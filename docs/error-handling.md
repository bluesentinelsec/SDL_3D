# Error Handling Strategy

SDL3D follows SDL3's existing error pattern.

- Functions return success or failure directly.
- Functions that fail set the calling thread's SDL error state with `SDL_SetError()` or an SDL helper macro such as `SDL_InvalidParamError()`.
- Callers must inspect the function return value first and only consult `SDL_GetError()` on failure.
- Successful SDL3D calls do not clear previous SDL error state automatically. This matches SDL's behavior and keeps error ownership predictable.

For API design, prefer:

- `bool` for operations that succeed or fail
- pointers or handles returned as `NULL` on failure
- SDL-owned error strings for human-readable diagnostics
