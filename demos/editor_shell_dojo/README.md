# Editor Shell Dojo

This data-only dojo demonstrates the first reusable editor shell pieces:

- a normal Slayer3D runtime viewport
- authored brush-world selection trace metadata with a ground work-plane
  fallback for placing the first brush in empty space
- data-authored world/selection/normal/hit debug overlay drawing, including a
  visible work-plane grid for empty-space placement
- reusable `ui.panels` and `ui.inspectors` populated from scene state
- a first blockout tool palette: `1` floor, `2` wall, `3` ceiling,
  `4` player start, then click a brush face or the ground work plane and press
  `Enter` to place the selected prefab at that point
- unified editable-level save/export: `S` saves and publishes one fragment
  containing the brush world and player starts
- test-run handoff manifest: `T` publishes and saves runner arguments for the
  authored player start
- generic runner test-run support: `--player-start player_start.editor_shell`
  enters the playable FPS scene through the same runtime path a data-only game
  uses

Run it with:

```sh
./build/debug/slayer3d_editor
```

The editor host is a thin wrapper around the generic runner. In the build tree it
defaults to this dojo, injects `editor.save.path` and `editor.test_run.path`,
and leaves all editor behavior data-authored. The equivalent runner command is:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json
```

Test-run the authored player start directly:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json --player-start player_start.editor_shell
```

By default, `S` writes
`demos/editor_shell_dojo/data/generated/editable_level.fragment.json`, which is
imported by the dojo on the next launch. `T` writes
`build/editor_shell_dojo/test-run.json`. Launch that saved manifest with:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --test-run-manifest build/editor_shell_dojo/test-run.json
```

For another editor project, pass explicit paths:

```sh
./build/debug/slayer3d_editor --root path/to/editor/data --data asset://editor.game.json --save path/to/generated/editable_level.fragment.json --test-run-output build/editor/test-run.json
```
