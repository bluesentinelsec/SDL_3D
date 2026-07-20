# AGENTS.md

This file gives working guidance for AI agents maintaining Slayer3D.

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

## Worktree and runtime consistency

- Treat the user-facing repository workspace as the canonical worktree for implementation, builds, and manual validation.
- Do not leave PR-significant changes only in `/tmp`, another clone, or an alternate worktree. If an alternate checkout is required for Git operations, synchronize the canonical workspace before asking the user to validate anything.
- At the start of work and before handoff, verify the repository root, branch, and commit with `git rev-parse --show-toplevel`, `git branch --show-current`, and `git rev-parse HEAD`.
- Build and test from the same worktree whose files will be reviewed and run. Do not use test results from one worktree to represent another without first verifying that the relevant files are identical.
- Treat configured build directories as source-worktree-specific. Check `CMAKE_HOME_DIRECTORY` in `CMakeCache.txt` before reusing a build directory from another checkout.
- The native Slayer3D editor compiles `SLAYER3D_EDITOR_DEFAULT_PROJECT` from the CMake source root and loads authored editor data from that path. Before requesting GUI validation, confirm the executable's configured project path points to the canonical workspace and rebuild after synchronizing source or data changes.
- When an alternate checkout cannot be avoided, state which checkout contains the PR branch and which path the executable loads. Do not hand off for validation until those paths refer to equivalent content.

## Suggested workflow

- Read the existing code and docs before editing.
- Make the most architecturally correct change that addresses the root cause.
- Add focused tests for the changed behavior.
- Run the relevant build/test targets before finishing.
- Update documentation or sample data if the public behavior changed.
- Run `clang-format` on all touched C and C++ files before submitting a PR.
- Before requesting user validation, confirm the canonical workspace contains the PR changes and that the validated executable was built from and loads data from that workspace.
- Always work through a pull request. Do not push directly to `main`.
- Do not force-push unless the user explicitly approves that exact push once for the current change.
