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
collapse or expand the panel.

A bottom console pane spans the editor width below the viewport. The Console
tab shows recent editor messages and mirrors those messages to the terminal via
SDL logging. The Issues tab is a placeholder for map findings such as leaks and
validation warnings.

The old number-key and `Tab` view switching shortcuts are intentionally
unbound. The editor viewport uses relative mouse motion for mouse-look. Brush
feedback is reticle-driven: the highlighted brush, hit marker, and placement
preview should remain pinned to the screen center instead of following the last
clicked selection.

## Keybindings

Tool and object selection should go through palettes so level-authoring
shortcuts do not shadow camera movement.

| Key | Context | Behavior |
| --- | --- | --- |
| `B` | global editor | enter Brush Paint Mode and open or close the Brushes palette |
| `M` | global editor | enter Texture Mode and open the Materials palette placeholder |
| `G` | global editor | open the Game Objects palette |
| `I` | global editor | collapse or expand the left Inspector panel |
| `Space` | editor layout, palette closed | enter Select Mode |
| arrow keys | palette open | move the active palette cursor |
| `Enter` | palette open | select the highlighted palette item and close the palette |
| `Esc` | palette open | close the palette without quitting |
| mouse click | Brush/Game Object Mode | place the selected prefab at the snapped preview position |
| mouse click | Select Mode | select or deselect the highlighted brush |
| `Shift` + mouse click | Select Mode | add or remove the highlighted brush from the selected set |
| right click | editor view | delete the highlighted tile or game object |
| `[` and `]` | Select Mode | lower or raise selected brush height/elevation by one active grid step |
| `Delete` / `Backspace` | Select Mode | delete all selected brushes |
| `Delete` / `Backspace` | other editor modes | delete the active highlighted tile or game object |
| `Enter` | palette closed | commit the current preview placement |
| `+` / `-` | palette closed | increase or decrease grid size through the fixed ladder: 0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256 |
| `R` | palette closed | toggle wall axis for manual wall placement |
| `C` | palette closed | cycle tools for debug/testing |
| `WASD` | 3D flyby | move camera |
| right mouse drag | 3D flyby | rotate camera without capturing the hardware cursor |
| middle mouse drag | 3D flyby | pan camera left/right/up/down |
| mouse wheel | 3D flyby | dolly camera forward/back |
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
4. Press `B`, move to Sky with the arrow keys, press `Enter`, and place a sky
   tile over an opening. The brush should seal leak validation; sky rendering
   will be revisited when a better editor skybox is authored.
5. Press `G`, press `Enter` to select Player Start, and place it on the floor.
6. Press `Space`, click a tile to select it, press `]`, and confirm the brush
   stretches upward while its bottom face stays anchored. Press `[` on a floor
   slab at the default floor plane and confirm the floor slab can move below
   the plane; the editor should add side-wall fill brushes around the exposed
   height change.
7. Enter the lowered area, switch back to Brush Paint Mode, and place floor,
   wall, and ceiling tiles. Their previews should stay on the lowered
   connected grid instead of snapping back to the original world plane.
8. Press `Delete` or `Backspace`; the tile should disappear. Shift-click
   multiple tiles to delete a set.
9. Press `Ctrl+S` or `Command+S` and confirm the output
   file should contain `brush_worlds`, `editor_brush_sources`, and `editor_player_starts`.
10. Press `F5`; the editor should validate the brush source model, then switch
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
11. In the playable scene, use `WASD` and mouse look to verify the player starts
   where you placed the marker and collides with the graybox geometry.

## Correctness Checks

The following should be true during a good test drive:

- Placement previews snap to the active grid size and resize when `+` / `-` is
  pressed. The second toolbar's Grid widget should update to the same value.
- The old left-side debug dump is no longer visible. The new left Inspector
  panel is visible with Map, Entity, and Face placeholder tabs, and `I`
  collapses it to a small strip.
- The bottom Console pane remains visible in the 3D editor view. It starts with
  `Editor ready`, appends common editor actions such as inspector toggles and
  saves, and prints the same messages to the terminal. The Issues tab is present
  as a placeholder for future map diagnostics.
- `B` enters Brush Paint Mode. `M` enters Texture Mode. Brush/material palette
  selections update the same data-authored mode and brush-setting state.
- `Space` enters Select Mode from editor layouts and the 3D flyby view. Select
  Mode owns brush selection and multi-selection. `[` and `]` resize generic
  selected brushes by the active grid step. Thin floor slabs can move below the
  default plane; the editor creates deterministic side-wall fill brushes for
  the exposed vertical space and removes the matching fill segment when raised
  back up.
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
- `B` opens a modal brush palette with floor, wall, ceiling, and sky cells. Arrow
  keys move the highlighted cell, `Enter` selects the brush, and `Esc` closes
  the modal without quitting the editor.
- `M` opens the placeholder Material palette. `G` opens the Game Objects
  palette, and `Enter` selects Player Start for placement.
- Placed player starts render as bright green cylinder markers in the editor
  canvas, can be highlighted by hovering, and can be removed with right click.
- The top menu toolbar and second tool toolbar are visible above every view
  mode, and the editor viewport starts below them instead of rendering
  underneath the toolbar chrome.
- The editor viewport displays a small label identifying the active 3D
  perspective view.
- Brush picking and placement trace from the screen center. The highlighted
  brush should be the brush under the crosshair; if no brush is hit, placement
  falls back to the ground work plane. After clicking one brush, look away and
  confirm the selection cursor follows the reticle rather than staying offset
  on the previous brush.
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
