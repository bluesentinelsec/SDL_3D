# Brush Geometry Dojo

Data-only showcase for native textured convex brush rendering, FPS brush
movement, trigger contents checks, and brush-world line-of-sight sensors.

Move with WASD, look with the mouse, and jump with Space. Walk through the cyan
trigger volume near the player start to verify `sensor.brush_contents`. The HUD
also reports brush line-of-sight sensor results for a visible target and an
occluded target behind the central pillar.

```sh
./build/debug/slayer3d_runner --root demos/brush_geometry_dojo/data --data asset://brush_geometry_dojo.game.json
```
