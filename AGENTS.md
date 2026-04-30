# AGENTS.md

This file gives working guidance for AI agents maintaining SDL_3D.

## Core expectations

- Prefer clean, human-readable, professional-grade C.
- Fix root causes. Do not leave obvious bugs in place.
- Do not paper over defects with workaround logic when a real fix is feasible.
- Breaking changes are acceptable before 1.0 when they improve correctness or API clarity.
- Keep the engine generalized and data driven. New features should extend the architecture rather than hard-code game-specific behavior where possible.

## Repository priorities

- Keep demos working.
- Keep tests passing.
- Keep docs in sync with behavior and public API changes.
- Update data files and runtime defaults together so authored content matches actual behavior.
- Favor reusable engine primitives over Pong-specific or demo-specific glue.

## Code quality rules

- Keep modules focused on a single responsibility.
- Prefer explicit, local logic over clever abstractions.
- Match existing project style and naming conventions.
- Document public APIs with Doxygen-style comments.
- Add or update tests when behavior changes.

## When changing behavior

- Verify the change against existing demos, not just the immediate target feature.
- If a bug is discovered along the way, fix it in the same change when practical.
- If a generalized engine helper can replace duplicated demo logic, prefer the helper.
- If a change affects authored data, update validation, examples, and docs together.

## Data-driven direction

- Treat JSON, Lua, and authored scene/data files as first-class game-definition inputs.
- Keep gameplay rules, menus, transitions, and reusable presentation behavior data-authored where possible.
- Reserve host-side C for true engine integration points and platform-specific work.

## Suggested workflow

- Read the existing code and docs before editing.
- Make the smallest correct change that addresses the root cause.
- Add focused tests for the changed behavior.
- Run the relevant build/test targets before finishing.
- Update documentation or sample data if the public behavior changed.

