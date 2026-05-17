# Editor Test Drive

This is the current graybox-editor milestone. It is still an alpha editor shell,
but it should prove the core editing loop: place floor/wall/ceiling blocks on
grid-cell boundaries, delete mistakes, place a player start, and jump into a
playable test scene using the same data-driven runtime used by games.

## Launch

Build the editor and open the default editor shell dojo:

```sh
cmake --build build/debug --target slayer3d_editor -j4
./build/debug/slayer3d_editor
```

Disk save and runner handoff are intentionally disabled while the blockout UX is
still changing quickly. The shell can export the current level JSON into scene
state for inspection, but it does not write generated maps to disk.

## Default Layout

The editor now starts in a classic four-viewport layout:

- Top-left: 3D perspective preview.
- Top-right: top/plan orthographic view for floor-plane placement.
- Bottom-left: front orthographic view.
- Bottom-right: side orthographic view.

Press `Q` to enter the full-screen 3D flyby viewport. Press `Q` again to return
to the four-viewport editor layout. `Tab` is also bound to the same toggle while
we evaluate the workflow.

The editor uses normal hardware cursor positioning in the four-viewport layout.
In full-screen 3D flyby mode the runtime captures relative mouse motion for
mouse-look, then releases it again when returning to orthographic editing.

## Controls

- `1`, `2`, `3`, `4`: select floor, wall, ceiling, or player-start prefab
- mouse click in an orthographic viewport: place the selected prefab at the
  snapped preview position
- right click a highlighted tile: delete it
- `Delete`: delete the active selected tile
- `Enter`: commit the current preview placement
- `+` / `-`: increase or decrease grid size
- `R`: toggle wall axis
- `Q` / `Tab`: toggle between four-view layout and full-screen 3D flyby
- `F1`, `F2`, `F3`, `F4`: perspective, top, front, side views
- in 3D flyby: `WASD` move, mouse look, `Space` / `Left Ctrl` move up/down,
  `Left Shift` moves faster
- `S`: export the editable level JSON in memory without writing it to disk
- `T`: reports that disk test-run handoff is disabled for this MVP iteration
- `F5`: enter the playable test scene in the editor from the placed player start
- `Esc`: quit

## Suggested Pass

1. Select floor with `1` and place a floor tile.
2. Select wall with `2`, press `R` if needed, and place at least one wall.
3. Select ceiling with `3` and place a ceiling tile.
4. Select player start with `4` and place it on the floor.
5. Hover a placed tile and right click, or select it and press `Delete`; the
   tile should disappear.
6. Press `S` and confirm the inspector reports an in-memory JSON export and a
   disabled disk-save message.
7. Press `F5`; the editor should switch into the playable test scene.
8. In the playable scene, use `WASD` and mouse look to verify the player starts
   where you placed the marker and collides with the graybox geometry.

## Correctness Checks

The following should be true during a good test drive:

- Placement previews snap to the active grid size and resize when `+` / `-` is
  pressed.
- The bright green placement preview sits under the mouse in orthographic
  views, using the containing grid cell rather than the nearest grid line.
- Floor, wall, and ceiling prefabs share the same grid-cell footprint and are
  anchored to grid-cell boundaries, so simple blockout maps can be tiled without
  visible gaps.
- Right click / `Delete` removes the highlighted brush and `Z` can undo the
  deletion.
- Floor, wall, and ceiling prefabs are distinct gray blockout brushes.
- The four-view layout appears on launch, and `Q` toggles into and out of the
  full-screen 3D flyby camera.
- Top/front/side orthographic views plot on their authored work planes;
  perspective preview uses the 3D editor camera.
- `S` exports JSON containing `brush_worlds` and `editor_player_starts` in
  memory only.
- `T` does not write a manifest during this MVP iteration.
- `F5` applies the stored player start before entering the playable scene.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, disk persistence is disabled, and
texture/face painting remains outside this first graybox milestone.
