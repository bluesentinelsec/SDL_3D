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

`--project` points at a directory containing `slayer3d.project.json`; the
manifest defines the data root and editor entry point. Press `S` in the editor
to atomically save an editable fragment containing `brush_worlds` and
`editor_player_starts`.

The equivalent raw runner command remains useful for data/runtime debugging:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json
```

See [docs/editor-test-drive.md](../../docs/editor-test-drive.md) for the current
keybindings and manual validation checklist.
