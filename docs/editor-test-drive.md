# Editor Test Drive

This is the current graybox-editor milestone. It is still an alpha editor shell,
but it should prove the core editing loop: place floor/wall/ceiling/sky blocks
on grid-cell boundaries, delete mistakes, place a player start, and jump into a
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

The editor now starts in a single 3D perspective/flyby viewport. Orthographic
views are temporarily not exposed while the editor UX focuses on making the 3D
workflow competent and predictable.

A nonfunctional top toolbar sits above the viewports with the first top-level
menus: File, Edit, Selection, Groups, Tools, View, Run, Debug, and Help.
Those buttons are visual placeholders for now; later slices will wire them to
menus such as File > Save without changing the viewport layout.

A second placeholder tool toolbar sits directly underneath it with the first
editor tool buttons: Select, Brush Tool, Clip Tool, Face Tool, Vertex Tool,
Entity Tool, and Texture Tool. These are also visual placeholders; keyboard
shortcuts still drive the active tools in this slice. The same toolbar also
shows the current grid size so placement and future transform operations have
an obvious snap reference.

The left Inspector panel is visible by default. It currently contains
placeholder Map, Entity, and Face tabs so the editor has a stable destination
for selection diagnostics and property editing in later slices. Press `I` to
collapse or expand the panel. Hovering a brush updates the Inspector with the
brush name, dimensions, and stable ID; use the stable ID when reporting geometry
issues because it persists through save/reopen.

A bottom console pane spans the editor width below the viewport. The Console
tab shows recent editor messages and mirrors those messages to the terminal via
SDL logging. The Issues tab is a placeholder for map findings such as leaks and
validation warnings.

The old number-key and `Tab` view switching shortcuts are intentionally
unbound. The editor viewport keeps the hardware cursor free for toolbar and
panel interaction. Brush feedback is cursor-driven: the highlighted brush and
flashing placement preview should follow the OS cursor without drawing a second
virtual hit cursor.

The editor starts in Select Mode with a `1` unit grid. In Select Mode, dragging
left mouse in empty space creates a source-backed box brush. The preview expands
while dragging, and release commits the same validated source command.

## Keybindings

Tool and object selection should go through palettes so level-authoring
shortcuts do not shadow camera movement.

| Key | Context | Behavior |
| --- | --- | --- |
| `B` | global editor | enter Brush Tool mode for brush creation |
| `M` | global editor | enter Texture Mode and open the Materials palette placeholder |
| `G` | global editor | open the Game Objects palette |
| `I` | global editor | collapse or expand the left Inspector panel |
| `Space` | editor layout, palette closed | enter Select Mode |
| arrow keys | palette open | move the active palette cursor |
| `Enter` | palette open | select the highlighted palette item and close the palette |
| `Esc` | palette open | close the palette without quitting |
| `Esc` | Brush Tool, nothing selected | return to Select Mode |
| `Esc` | any mode with a selection | clear the selection |
| left mouse drag | Select Mode, empty space | create a source-backed box brush on the active grid; a single click only clears/changes selection |
| left mouse drag | Select Mode, selected brush | move selected brushes with snap-to-grid |
| left mouse drag | Face Tool, highlighted face | push or pull the highlighted source-brush face with snap-to-grid |
| `Shift` + hover | Brush Tool, highlighted face | show the face outline as ready for a drawn-face brush operation |
| `Alt`/`Option` + left mouse drag | Select Mode, selected brush | move selected brushes only on the Y axis with snap-to-grid |
| arrow keys | Select Mode, brush selected | move selected brushes by the active grid size on Z/X (`Up` moves +X, `Down` moves -X) |
| `PageUp` / `PageDown` | Select Mode, brush selected | move selected brushes up/down on Y by the active grid size (`Fn+Up` / `Fn+Down` on many macOS keyboards) |
| `Ctrl`/`Alt` + `Up`/`Down` | Select Mode, brush selected | move selected brushes up/down on Y by the active grid size on keyboards without Fn navigation keys |
| `Home` / `End` | Select Mode, brush selected | rotate selected brushes around their center in 90-degree Y-axis steps (`Fn+Left` / `Fn+Right` on many macOS keyboards) |
| `Ctrl`/`Alt` + `Left`/`Right` | Select Mode, brush selected | rotate selected brushes around their center in 90-degree Y-axis steps on keyboards without Fn navigation keys |
| mouse click | Select Mode | select or deselect the highlighted brush |
| `Shift` + mouse click | Select Mode | add or remove the highlighted brush from the selected set |
| `Delete` / `Backspace` | Select Mode | delete all selected brushes |
| `Delete` / `Backspace` | other editor modes | delete the active highlighted tile or game object |
| `Enter` | palette closed | commit the current prefab preview placement |
| `+` / `-` | palette closed | increase or decrease grid size through the fixed ladder: 0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256 |
| Grid widget | second toolbar | click to open the grid-size list, then click a row to set the active grid |
| Prefabs widget | second toolbar | open common blockout prefab shapes such as floor, wall, ceiling, and sky; future entries will include stairs, ramps, windows, arches, and doorways |
| `R` | palette closed | toggle wall axis for manual wall placement |
| `C` | palette closed | cycle tools for debug/testing |
| `WASD` | 3D flyby | move camera |
| right mouse drag | 3D flyby | rotate camera without capturing the hardware cursor |
| `Alt`/`Option` + right mouse drag | 3D flyby | orbit around the hovered brush or work-plane point |
| middle mouse drag | 3D flyby | pan camera left/right/up/down |
| mouse wheel | 3D flyby | dolly camera forward/back |
| `Q` / `E` | 3D flyby | move camera down/up |
| `Left Shift` | 3D flyby | move faster |
| `Ctrl+U` / `Command+U` | 3D flyby | frame the selected or hovered brush in view |
| `Ctrl+S` / `Command+S` | palette closed | export editable level JSON in memory and save it to the CLI output path |
| `T` | palette closed | report that disk test-run manifest handoff is disabled for this MVP iteration |
| `F5` | palette closed | enter the playable test scene from the placed player start |
| `Esc` | palette closed | clear selection or leave Brush Tool; it does not quit the editor |

## Suggested Pass

The shell project intentionally starts empty: no starter cube and no default
player-start marker. This keeps every visible brush and marker attributable to
your current editing session.

1. In Select Mode, drag left mouse in empty space and release. A new box brush
   should be created using the active `1` unit grid.
2. Click the new brush, then click empty space. The selection should clear
   without creating a brush. Brush creation from the pointer requires a
   left-button drag, not a plain click.
3. Select the brush again, then use the arrow keys to nudge it by the active grid
   size. Use `PageUp` / `PageDown` or macOS `Fn+Up` / `Fn+Down` to move it
   vertically, and use `Home` / `End` or macOS `Fn+Left` / `Fn+Right` to rotate
   it in 90-degree Y-axis steps. Drag the selected brush with left mouse and
   confirm it moves on the same grid.
4. Click `Prefabs`, leave Floor selected, press `Enter`, and place a floor tile.
5. Click `Prefabs`, move to Wall with the arrow keys, press `Enter`, press `R`
   if needed, and place at least one wall.
6. Click `Prefabs`, move to Ceiling with the arrow keys, press `Enter`, and
   place a ceiling tile.
7. Click `Prefabs`, move to Sky with the arrow keys, press `Enter`, and place a
   sky tile over an opening. The brush should seal leak validation; sky
   rendering will be revisited when a better editor skybox is authored.
8. Press `G`, press `Enter` to select Player Start, and place it on the floor.
9. Press `Space`, click a tile to select it, press `]`, and confirm the brush
   stretches upward while its bottom face stays anchored. Press `[` on a floor
   slab at the default floor plane and confirm the floor slab can move below
   the plane; the editor should add side-wall fill brushes around the exposed
   height change.
10. Enter the lowered area, choose prefabs again, and place floor, wall, and
   ceiling tiles. Their previews should stay on the lowered
   connected grid instead of snapping back to the original world plane.
11. Press `Delete` or `Backspace`; the tile should disappear. Shift-click
   multiple tiles to delete a set.
12. Press `Ctrl+S` or `Command+S` and confirm the output
   file should contain `brush_worlds`, `editor_brush_sources`, and `editor_player_starts`.
13. Press `F5`; the editor should validate the brush source model, then switch
   into the playable test scene using the current in-memory map and player
   start. Source overlaps, near-gaps, and reachable leaks are warning-level
   diagnostics during this MVP iteration: debug markers and scene-state
   diagnostics should still be available, but they should not block entering
   the playable test scene. Reachable
   leaks also draw a bright magenta diagnostic marker at the first
   outside-boundary cell so you have a concrete place to inspect, plus an
   orange candidate marker on the nearest source-box face associated with that
   leak boundary. The candidate brush face should also become the active
   selection, making the suspected source face obvious in the selection outline.
14. In the playable scene, use `WASD` and mouse look to verify the player starts
   where you placed the marker and collides with the graybox geometry.

## Correctness Checks

The following should be true during a good test drive:

- Placement previews snap to the active grid size and resize when `+` / `-` is
  pressed. The second toolbar's Grid widget should update to the same value.
- The default grid size is `1`. Select Mode left-drag creation, selected-brush dragging, brush-mode
  placement previews, and selected-brush arrow-key movement should all use the
  same active grid value.
- The old left-side debug dump is no longer visible. The new left Inspector
  panel is visible with Map, Entity, and Face placeholder tabs, and `I`
  collapses it to a small strip.
- The bottom Console pane remains visible in the 3D editor view. It starts with
  `Editor ready`, appends common editor actions such as inspector toggles and
  saves, and prints the same messages to the terminal. The Issues tab is present
  as a placeholder for future map diagnostics.
- `B` enters Brush Tool mode. Hovered brush faces render with yellow face
  outlines and vertex handles as the first step toward TrenchBroom-style
  face-based brush creation. The Prefabs toolbar widget owns
  floor/wall/ceiling/sky blockout shapes and is intentionally separate from
  Brush Tool.
- Face Tool owns face push/pull editing. Dragging a highlighted face previews
  and commits a source face resize with snap-to-grid.
- `M` enters Texture Mode. Material palette selections update the same
  data-authored mode and brush-setting state.
- `Space` enters Select Mode from editor layouts and the 3D flyby view. Select
  Mode owns brush selection and multi-selection. Arrow-key movement, vertical
  movement, and 90-degree Y-axis rotation all operate on source brushes by the
  active grid step.
- Game object placement uses its own tighter snap grid, so player starts can be
  placed precisely while floor/wall/ceiling/sky brushes stay aligned to the larger
  blockout grid.
- Floor and ceiling prefabs share the same grid-cell footprint. Wall prefabs
  are thin grid-edge segments, so simple blockout maps can be tiled without
  visible gaps while adjacent hallways can pass close to one another. Wall
  bottoms sit directly on the connected floor elevation; authored blockout
  seams should be watertight rather than relying on visual offsets.
- Floor, wall, ceiling, and sky placements commit as canonical source-box
  operations. Preview and commit use the same source candidate; valid previews
  should commit with matching bounds, and true invalid intersections should
  report a source-model diagnostic instead of a vague runtime overlap failure.
- In 3D flyby mode, wall placement auto-rotates to the nearest cardinal axis
  from the camera direction.
- Floor, wall, ceiling, and sky previews share a connected-grid elevation. Hovering
  or working from a lowered floor or one of its side-wall fills should update
  the editor brush elevation and work plane so new floor, wall, and ceiling
  tiles can continue the lower level without manual state edits.
- Right click / `Delete` removes the highlighted brush and `U` can undo the
  deletion.
- Floor, wall, ceiling, and sky prefabs are distinct blockout brushes.
- The `Prefabs` toolbar widget opens a modal prefab palette with floor, wall,
  ceiling, and sky cells. Arrow keys move the highlighted cell, `Enter` selects
  the prefab, and `Esc` closes the modal without quitting the editor.
- `M` opens the placeholder Material palette. `G` opens the Game Objects
  palette, and `Enter` selects Player Start for placement.
- Placed player starts render as bright green cylinder markers in the editor
  canvas, can be highlighted by hovering, and can be removed after selection
  with `Delete` or `Backspace`.
- The top menu toolbar and second tool toolbar are visible above every view
  mode, and the editor viewport starts below them instead of rendering
  underneath the toolbar chrome.
- The editor viewport displays a small label identifying the active 3D
  perspective view.
- Brush picking and placement trace from the hardware cursor. The highlighted
  brush should be the brush under the cursor; if no brush is hit, placement
  falls back to the ground work plane. In brush mode, the flashing preview
  bounds should move as the cursor moves, but a plain click in empty space
  should not commit a brush.
- `Ctrl+S` and `Command+S` export JSON containing `brush_worlds`, `editor_brush_sources`, and
  `editor_player_starts`, then writes the same fragment to the CLI output path.
- `T` does not write a test-run manifest during this MVP iteration.
- `F5` validates source-backed brush structure, then applies the stored player
  start before entering the playable scene. Source overlaps, near-gaps, and
  leaks are surfaced as warnings while this foundation is still being built;
  hard source defects such as invalid geometry, duplicate identifiers, or
  off-snap coordinates still prevent source edits from committing.

Current limitations are expected: this is not yet a polished TrenchBroom-style
frontend, there is no file picker, and texture/face painting remains outside
this first graybox milestone.
