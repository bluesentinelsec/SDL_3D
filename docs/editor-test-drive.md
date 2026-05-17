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

## Default Layout

The editor now starts in a classic four-viewport layout:

- Top-left: 3D perspective preview.
- Top-right: top/plan orthographic view for floor-plane placement.
- Bottom-left: front orthographic view.
- Bottom-right: side orthographic view.

Press `Q` to enter the full-screen 3D flyby viewport. Press `Q` again to return
to the four-viewport editor layout. `Tab` is also bound to the same toggle while
we evaluate the workflow.

## Controls

- `1`, `2`, `3`, `4`: select floor, wall, ceiling, or player-start prefab
- mouse click in an orthographic viewport: place the selected prefab at the
  snapped preview position
- `Enter`: commit the current preview placement
- `+` / `-`: increase or decrease grid size
- `R`: toggle wall axis
- `Q` / `Tab`: toggle between four-view layout and full-screen 3D flyby
- `F1`, `F2`, `F3`, `F4`: perspective, top, front, side views
- in 3D flyby: `WASD` move, mouse look, `Space` / `Left Ctrl` move up/down,
  `Left Shift` moves faster
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
- Floor, wall, and ceiling prefabs share the same grid-cell footprint and are
  anchored to grid-cell boundaries, so simple blockout maps can be tiled without
  visible gaps.
- Floor, wall, and ceiling prefabs are distinct gray blockout brushes.
- The four-view layout appears on launch, and `Q` toggles into and out of the
  full-screen 3D flyby camera.
- Top/front/side orthographic views plot on their authored work planes;
  perspective preview uses the 3D editor camera.
- `S` writes a JSON fragment containing `brush_worlds` and
  `editor_player_starts`.
- `T` saves the latest dirty level before writing the test-run manifest.
- `F5` applies the stored player start before entering the playable scene.
- The standalone runner command loads the same saved geometry and player start.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, and texture/face painting remains outside
this first graybox milestone.
