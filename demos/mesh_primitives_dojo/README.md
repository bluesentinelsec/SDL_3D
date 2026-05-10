# Mesh Primitives Dojo

Data-only graybox showcase for the procedural `render.mesh_primitive` component.
It runs through the generic SDL3D runner and contains no demo-specific host code.

```sh
build/debug/sdl3d_runner \
  --root demos/mesh_primitives_dojo/data \
  --data asset://mesh_primitives_dojo.game.json
```

The room is a large sector-bounded square using untextured gray materials, a
single directional sun light, an FPS camera/controller, one authored entity for
each primitive kind, and a few mock actors assembled with `render.composite`.
