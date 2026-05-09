# FPS Sector Template

These fragments are project-agnostic starting points for first-person sector
games. Copy the fragments into a game data tree, rename ids where needed, and
import only the sections the project uses.

The template intentionally uses generic names such as `action.move.forward`,
`action.fire`, `render.profile`, and `entity.player`. Games can keep those
conventions or map them to their own names.

Suggested import order:

```json
[
  { "path": "fragments/input/fps_controls.json", "sections": ["input"] },
  { "path": "fragments/presentation/render_profile_controls.json", "sections": ["signals", "logic"] },
  { "path": "fragments/actors/fps_projectile_weapon.json", "sections": ["actor_archetypes", "actor_pools"] }
]
```

The weapon fragment expects the player entity to author a `weapon.projectile`
component or equivalent `projectile.fire` action. The component is preferred
for data-authored FPS weapons.
