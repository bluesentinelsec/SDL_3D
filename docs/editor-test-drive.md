# Editor Test Drive

This is the current graybox-editor milestone. It is still an alpha editor shell,
but it should prove the core loop: place floor/wall/ceiling blocks, place a
player start, save the authored level fragment, and run the result through the
same data-driven runtime used by games.

## Launch

Build the editor and open the default editor shell dojo:

```sh
cmake --build build/debug --target slayer3d_editor -j4
./build/debug/slayer3d_editor
```

The default editor project writes:

- level fragment: `demos/editor_shell_dojo/data/generated/editable_level.fragment.json`
- test-run manifest: `build/editor_shell_dojo/test-run.json`

## Controls

- `1`, `2`, `3`, `4`: select floor, wall, ceiling, or player-start prefab
- mouse click: place the selected prefab at the snapped preview position
- `Enter`: commit the current preview placement
- `+` / `-`: increase or decrease grid size
- `R`: toggle wall axis
- `Tab`: toggle editor view
- `F1`, `F2`, `F3`, `F4`: perspective, top, front, side views
- arrow keys: move the editor camera
- `Q` / `E`: move camera down/up
- hold right mouse: look around in the perspective editor camera
- `S`: save the editable level fragment
- `T`: save the level, write the test-run manifest, and show the runner command
- `F5`: enter the playable test scene in the editor from the placed player start
- `Esc`: quit

## Suggested Pass

1. Select floor with `1` and place a floor tile.
2. Select wall with `2`, press `R` if needed, and place at least one wall.
3. Select ceiling with `3` and place a ceiling tile.
4. Select player start with `4` and place it on the floor.
5. Press `S` and confirm the inspector reports a saved level path.
6. Press `T` and confirm the bottom handoff strip appears with level path,
   manifest path, and a copy/paste runner command.
7. Press `F5`; the editor should switch into the playable test scene.
8. In the playable scene, use `WASD` and mouse look to verify the player starts
   where you placed the marker and collides with the graybox geometry.

You can also run the handoff through the generic runner:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --test-run-manifest build/editor_shell_dojo/test-run.json
```

## Correctness Checks

The following should be true during a good test drive:

- Placement previews snap to the active grid size and resize when `+` / `-` is
  pressed.
- Floor, wall, and ceiling prefabs are distinct gray blockout brushes.
- Top/front/side views plot on the correct work plane; perspective view uses the
  3D editor camera.
- `S` writes a JSON fragment containing `brush_worlds` and
  `editor_player_starts`.
- `T` saves the latest dirty level before writing the test-run manifest.
- `F5` applies the stored player start before entering the playable scene.
- The standalone runner command loads the same saved geometry and player start.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, and texture/face painting remains outside
this first graybox milestone.
