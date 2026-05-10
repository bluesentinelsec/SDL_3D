# Slayer 3D FPS Template

This template is a data-only starter for sector/FPS projects. It is intended to
be copied into a new game project and customized through JSON and Lua.

Run it with:

```sh
./build/debug/slayer3d_runner --root demos/templates/fps/data --data asset://fps_template.game.json
```

The fragments demonstrate common project-agnostic conventions:

- FPS keyboard/mouse input actions.
- A sector-backed player controller.
- A minimal sector room and navigation graph.
- Reticle, FPS counter, resource/combat debug HUD, and pause overlay.
- Projectile, pickup, and station archetypes with editor metadata.
