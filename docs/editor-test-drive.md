# Editor Test Drive

This is the current graybox-editor milestone. It is still an alpha editor shell,
but it should prove the core editing loop: place floor/wall/ceiling blocks on
grid-cell boundaries, delete mistakes, place a player start, and jump into a
playable test scene using the same data-driven runtime used by games.

## Launch

Build the editor and open the default editor shell dojo:

```sh
cmake --build build/debug --target slayer3d_editor -j4
./build/debug/slayer3d_editor new --project demos/editor_shell_dojo --output /tmp/slayer3d-test-level.fragment.json --overwrite
```

The editor now uses explicit subcommands. `new` starts from the project
template, and `open` loads an existing editable level fragment:

```sh
./build/debug/slayer3d_editor open --project demos/editor_shell_dojo --input /tmp/slayer3d-test-level.fragment.json
```

The project path points at a directory containing `slayer3d.project.json`.
Saving uses an atomic same-directory temporary file and rename; `open` without
`--output` saves back to `--input`. Use `--output` with `open` when you want to
fork an existing editable fragment into a different file, and pass
`--overwrite` only when replacing an existing output path is intentional.

The editor host is a thin wrapper around the generic runner. For runtime/data
debugging, the equivalent raw runner command for opening a saved level is:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json --state editor.command=open --state editor.input.path=/tmp/slayer3d-test-level.fragment.json --state editor.save.path=/tmp/slayer3d-test-level.fragment.json
```

The raw runner path is useful for debugging state injection, but normal editing
should use `slayer3d_editor new` or `slayer3d_editor open`.

## Default Layout

The editor now starts in a classic four-viewport layout:

- Top-left: 3D perspective preview.
- Top-right: top/plan orthographic view for floor-plane placement.
- Bottom-left: front orthographic view.
- Bottom-right: side orthographic view.

Use the number keys to switch layouts: `1` returns to the four-viewport layout,
`2` opens the full-screen 3D flyby viewport, `3` opens top orthographic, `4`
opens front orthographic, and `5` opens side orthographic. `Tab` remains a
quick toggle between the four-viewport layout and full-screen 3D flyby.

The editor uses normal hardware cursor positioning in the four-viewport layout.
In full-screen 3D flyby mode the runtime captures relative mouse motion for
mouse-look, then releases it again when returning to orthographic editing.
Flyby brush feedback is reticle-driven: the highlighted brush, hit marker, and
placement preview should remain pinned to the screen center instead of following
the last clicked selection.

## Keybindings

The number row is reserved for view selection. Tool and object selection should
go through palettes so level-authoring shortcuts do not shadow view controls.

| Key | Context | Behavior |
| --- | --- | --- |
| `1` | global editor | four-viewport layout |
| `2` | global editor | full-screen 3D perspective / flyby view |
| `3` | global editor | full-screen top orthographic view |
| `4` | global editor | full-screen front orthographic view |
| `5` | global editor | full-screen side orthographic view |
| `Tab` | global editor | quick toggle between four-view layout and full-screen 3D flyby |
| `B` | global editor | enter Brush Paint Mode and open or close the Brushes palette |
| `M` | global editor | enter Texture Mode and open the Materials palette placeholder |
| `G` | global editor | open the Game Objects palette |
| `Space` | editor layout, palette closed | enter Select Mode |
| arrow keys | palette open | move the active palette cursor |
| `Enter` | palette open | select the highlighted palette item and close the palette |
| `Esc` | palette open | close the palette without quitting |
| mouse click | Brush/Game Object Mode | place the selected prefab at the snapped preview position |
| mouse click | Select Mode | select or deselect the highlighted brush |
| `Shift` + mouse click | Select Mode | add or remove the highlighted brush from the selected set |
| right click | orthographic view | delete the highlighted tile or game object |
| `[` and `]` | Select Mode | lower or raise selected brush height/elevation by one active grid step |
| `Delete` / `Backspace` | Select Mode | delete all selected brushes |
| `Delete` / `Backspace` | other editor modes | delete the active highlighted tile or game object |
| `Enter` | palette closed | commit the current preview placement |
| `+` / `-` | palette closed | increase or decrease grid size |
| arrow keys | full-screen orthographic, palette closed | pan the active canvas |
| `Z` / `X` or mouse wheel | orthographic view | zoom the active canvas in or out; in four-view layout, the mouse cursor must be over an orthographic pane |
| `R` | palette closed | toggle wall axis for orthographic/manual wall placement |
| `C` | palette closed | cycle tools for debug/testing |
| `WASD` | 3D flyby | move camera |
| mouse look | 3D flyby | rotate camera |
| `Q` / `E` | 3D flyby | move camera down/up |
| `Left Shift` | 3D flyby | move faster |
| `Ctrl+S` / `Command+S` | palette closed | export editable level JSON in memory and save it to the CLI output path |
| `T` | palette closed | report that disk test-run manifest handoff is disabled for this MVP iteration |
| `F5` | palette closed | enter the playable test scene from the placed player start |
| `Esc` | palette closed | quit |

## Suggested Pass

1. Press `B`, leave Floor selected, press `Enter`, and place a floor tile.
2. Press `B`, move to Wall with the arrow keys, press `Enter`, press `R` if
   needed, and place at least one wall.
3. Press `B`, move to Ceiling with the arrow keys, press `Enter`, and place a
   ceiling tile.
4. Press `G`, press `Enter` to select Player Start, and place it on the floor.
5. Press `Space`, click a tile to select it, press `]`, and confirm the brush
   stretches upward while its bottom face stays anchored. Press `[` on a floor
   slab at the default floor plane and confirm the floor slab can move below
   the plane; the editor should add side-wall fill brushes around the exposed
   height change.
6. Enter the lowered area, switch back to Brush Paint Mode, and place floor,
   wall, and ceiling tiles. Their previews should stay on the lowered
   connected grid instead of snapping back to the original world plane.
7. Press `Delete` or `Backspace`; the tile should disappear. Shift-click
   multiple tiles to delete a set.
8. Press `Ctrl+S` or `Command+S` and confirm the inspector reports a successful save. The output
   file should contain `brush_worlds` and `editor_player_starts`.
9. Press `F5`; the editor should switch into the playable test scene using the
   current in-memory map and player start.
10. In the playable scene, use `WASD` and mouse look to verify the player starts
   where you placed the marker and collides with the graybox geometry.

## Correctness Checks

The following should be true during a good test drive:

- Placement previews snap to the active grid size and resize when `+` / `-` is
  pressed.
- The inspector reports explicit editor mode and brush settings: active prefab,
  grid size, height, elevation, thickness, and material.
- `B` enters Brush Paint Mode. `M` enters Texture Mode. Brush/material palette
  selections update the same data-authored mode and brush-setting state.
- `Space` enters Select Mode from editor layouts and the 3D flyby view. Select
  Mode owns brush selection and multi-selection. `[` and `]` resize generic
  selected brushes by the active grid step. Thin floor slabs can move below the
  default plane; the editor creates deterministic side-wall fill brushes for
  the exposed vertical space and removes the matching fill segment when raised
  back up.
- Game object placement uses its own tighter snap grid, so player starts can be
  placed precisely while floor/wall/ceiling brushes stay aligned to the larger
  blockout grid.
- The bright green placement preview sits under the mouse in orthographic
  views, using the containing grid cell rather than the nearest grid line.
- Floor and ceiling prefabs share the same grid-cell footprint. Wall prefabs
  are thin grid-edge segments, so simple blockout maps can be tiled without
  visible gaps while adjacent hallways can pass close to one another.
- In full-screen 3D flyby mode, wall placement auto-rotates to the nearest
  cardinal axis from the camera direction. Orthographic placement keeps the
  explicit `R` wall-axis toggle for drafting control.
- Floor, wall, and ceiling previews share a connected-grid elevation. Hovering
  or working from a lowered floor or one of its side-wall fills should update
  the editor brush elevation and work plane so new floor, wall, and ceiling
  tiles can continue the lower level without manual state edits.
- Right click / `Delete` removes the highlighted brush and `U` can undo the
  deletion.
- Floor, wall, and ceiling prefabs are distinct gray blockout brushes.
- `B` opens a modal brush palette with floor, wall, and ceiling cells. Arrow
  keys move the highlighted cell, `Enter` selects the brush, and `Esc` closes
  the modal without quitting the editor.
- `M` opens the placeholder Material palette. `G` opens the Game Objects
  palette, and `Enter` selects Player Start for placement.
- Placed player starts render as bright green cylinder markers in the editor
  canvas, can be highlighted by hovering, and can be removed with right click.
- The four-view layout appears on launch. Number keys switch between quad,
  full-screen 3D, and full-screen orthographic views.
- Each viewport displays a small label identifying the active view: 3D
  perspective, top/XY, front/XZ, or side/YZ.
- Full-screen orthographic views pan with the arrow keys and zoom with `Z`,
  `X`, or the mouse wheel while keeping a visible work grid.
- In the four-view layout, mouse wheel zoom works when the cursor is over the
  top, front, or side orthographic pane. The 3D perspective pane does not
  consume orthographic zoom.
- Top/front/side orthographic views plot on their authored work planes;
  perspective preview uses the 3D editor camera.
- In full-screen 3D flyby mode, brush picking and placement trace from the
  screen center. The highlighted brush should be the brush under the crosshair;
  if no brush is hit, placement falls back to the ground work plane. After
  clicking one brush, look away and confirm the selection cursor follows the
  reticle rather than staying offset on the previous brush.
- `Ctrl+S` and `Command+S` export JSON containing `brush_worlds` and `editor_player_starts` and
  writes the same fragment to the CLI output path.
- `T` does not write a test-run manifest during this MVP iteration.
- `F5` applies the stored player start before entering the playable scene.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, and texture/face painting remains outside
this first graybox milestone.
