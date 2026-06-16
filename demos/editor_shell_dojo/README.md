# Editor Shell Dojo

This data-only dojo demonstrates the reusable graybox editor shell: multiple
viewports, orthographic grid placement, brush/game-object palettes, editable
brush-world export, player-start placement, and an in-runtime playable test
scene.

Run a new editing session:

```sh
./build/debug/slayer3d_editor new --project demos/editor_shell_dojo --output /tmp/slayer3d-editor-level.fragment.json --overwrite
```

Reopen that saved level:

```sh
./build/debug/slayer3d_editor open --project demos/editor_shell_dojo --input /tmp/slayer3d-editor-level.fragment.json
```

Use a custom texture directory for a session:

```sh
./build/debug/slayer3d_editor new --project demos/editor_shell_dojo --output /tmp/slayer3d-editor-level.fragment.json --texture-path /path/to/textures --overwrite
```

`--project` points at a directory containing `slayer3d.project.json`; the
manifest defines the data root, editor entry point, optional media root, and
optional test-run manifest path. It can also define `asset_sources` for
textures, models, sprites, skyboxes, and effects. Each source is resolved
relative to the project manifest unless it is already absolute; the editor
publishes both the resolved path and the project-relative path to scene state so
future browsers and saved maps can prefer project-relative references.
`--texture-path` overrides only the texture source for the current session and
becomes the one authoritative texture directory. The directory may contain image
files directly or in nested subdirectories. `new` requires `--output`. `open`
requires `--input` and saves back to that same file unless `--output` is
supplied. Pass `--overwrite` only when replacing an existing output file is
intentional.

Press `Ctrl+S` or `Command+S` in the editor to atomically save an editable
fragment containing `brush_worlds` and `editor_player_starts`. Press `F5` to
enter the in-runtime playable test scene from the current in-memory level.

The equivalent raw runner command remains useful for data/runtime debugging:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json --state editor.command=open --state editor.input.path=/tmp/slayer3d-editor-level.fragment.json --state editor.save.path=/tmp/slayer3d-editor-level.fragment.json
```

## Clip Tool Fixture

Open the clip-tool verification fixture into a scratch output file:

```sh
./build/debug/slayer3d_editor open --project demos/editor_shell_dojo --input demos/editor_shell_dojo/data/fixtures/clip_tool.fragment.json --output /tmp/slayer3d-clip-tool.fragment.json --overwrite
```

The fixture loads a small source-backed brush world with a floor, a bevel cube,
a wall strip, a terrain block, and two side-by-side blocks for multi-brush
checks. Save and reopen the scratch file, not the fixture, when validating
round-trip behavior.

Current clip-tool keys:

- `C`: enter clip mode for the selected brush or brushes.
- Left click: place clip points. Two points use the brush face/work-plane
  normal captured when the point is placed; three points define the clipping
  plane exactly.
- Left-drag a clip point: move the point on its captured plane; points snap to
  source vertices, edges, faces, and the grid.
- `Ctrl+Enter` or `Command+Enter`: cycle keep-front, keep-back, and keep-both
  split modes.
- `Enter`: apply the clip as one undoable edit.
- `Esc`: cancel the active clip operation or exit the tool.
- `U`: undo the last editor transaction.
- `Y`: redo the last undone editor transaction.
- `Ctrl+S` or `Command+S`: save the editable fragment atomically.
- `F5`: test-run the current in-memory level.

Manual clip-tool verification:

- Bevel a cube corner.
- Carve a doorway by splitting a wall into side/top pieces and removing the
  opening.
- Split the wall strip into two pieces with keep-both mode.
- Clip the terrain block into a diagonal wedge.
- Select and clip the two side-by-side blocks at once.
- Undo and redo each operation.
- Save the scratch fragment, reopen it, and verify the clipped source brushes
  still match the edited shapes.
- Press `F5` and inspect the compiled test-run output for visible z-fighting,
  corrupt geometry, or collision anomalies.
