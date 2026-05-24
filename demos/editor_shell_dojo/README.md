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
manifest defines the data root, editor entry point, optional media root, and
optional test-run manifest path. `new` requires `--output`. `open` requires
`--input` and saves back to that same file unless `--output` is supplied. Pass
`--overwrite` only when replacing an existing output file is intentional.

Press `Ctrl+S` or `Command+S` in the editor to atomically save an editable
fragment containing `brush_worlds` and `editor_player_starts`. Press `F5` to
enter the in-runtime playable test scene from the current in-memory level.

The equivalent raw runner command remains useful for data/runtime debugging:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json --state editor.command=open --state editor.input.path=/tmp/slayer3d-editor-level.fragment.json --state editor.save.path=/tmp/slayer3d-editor-level.fragment.json
```

The editor UI exposes the current keybindings in its in-scene help text. Keep
this dojo README focused on launch/save behavior so editor workflow details can
evolve in the authored data and engine tests.
