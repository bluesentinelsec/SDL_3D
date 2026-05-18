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

Use the number keys to switch layouts: `1` returns to the four-viewport layout,
`2` opens the full-screen 3D flyby viewport, `3` opens top orthographic, `4`
opens front orthographic, and `5` opens side orthographic. `Tab` remains a
quick toggle between the four-viewport layout and full-screen 3D flyby.

The editor uses normal hardware cursor positioning in the four-viewport layout.
In full-screen 3D flyby mode the runtime captures relative mouse motion for
mouse-look, then releases it again when returning to orthographic editing.

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
| `B` | global editor | open or close the Brushes palette |
| `M` | global editor | open the Materials palette placeholder |
| `G` | global editor | open the Game Objects palette |
| arrow keys | palette open | move the active palette cursor |
| `Enter` | palette open | select the highlighted palette item and close the palette |
| `Esc` | palette open | close the palette without quitting |
| mouse click | orthographic view | place the selected prefab at the snapped preview position |
| right click | orthographic view | delete the highlighted tile |
| `Delete` | orthographic view | delete the active selected tile |
| `Enter` | palette closed | commit the current preview placement |
| `+` / `-` | palette closed | increase or decrease grid size |
| arrow keys | full-screen orthographic, palette closed | pan the active canvas |
| `Z` / `X` or mouse wheel | full-screen orthographic | zoom the active canvas in or out |
| `R` | palette closed | toggle wall axis |
| `C` | palette closed | cycle tools for debug/testing |
| `WASD` | 3D flyby | move camera |
| mouse look | 3D flyby | rotate camera |
| `Space` / `Left Ctrl` | 3D flyby | move camera up/down |
| `Left Shift` | 3D flyby | move faster |
| `S` | palette closed | export editable level JSON in memory without writing it to disk |
| `T` | palette closed | report that disk test-run handoff is disabled for this MVP iteration |
| `F5` | palette closed | enter the playable test scene from the placed player start |
| `Esc` | palette closed | quit |

## Suggested Pass

1. Press `B`, leave Floor selected, press `Enter`, and place a floor tile.
2. Press `B`, move to Wall with the arrow keys, press `Enter`, press `R` if
   needed, and place at least one wall.
3. Press `B`, move to Ceiling with the arrow keys, press `Enter`, and place a
   ceiling tile.
4. Press `G`, press `Enter` to select Player Start, and place it on the floor.
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
- Right click / `Delete` removes the highlighted brush and `U` can undo the
  deletion.
- Floor, wall, and ceiling prefabs are distinct gray blockout brushes.
- `B` opens a modal brush palette with floor, wall, and ceiling cells. Arrow
  keys move the highlighted cell, `Enter` selects the brush, and `Esc` closes
  the modal without quitting the editor.
- `M` opens the placeholder Material palette. `G` opens the Game Objects
  palette, and `Enter` selects Player Start for placement.
- The four-view layout appears on launch. Number keys switch between quad,
  full-screen 3D, and full-screen orthographic views.
- Each viewport displays a small label identifying the active view: 3D
  perspective, top/XY, front/XZ, or side/YZ.
- Full-screen orthographic views pan with the arrow keys and zoom with `Z`,
  `X`, or the mouse wheel while keeping a visible work grid.
- Top/front/side orthographic views plot on their authored work planes;
  perspective preview uses the 3D editor camera.
- `S` exports JSON containing `brush_worlds` and `editor_player_starts` in
  memory only.
- `T` does not write a manifest during this MVP iteration.
- `F5` applies the stored player start before entering the playable scene.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, disk persistence is disabled, and
texture/face painting remains outside this first graybox milestone.
