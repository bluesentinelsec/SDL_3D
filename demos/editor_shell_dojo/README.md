# Editor Shell Dojo

This data-only dojo demonstrates the first reusable editor shell pieces:

- a normal Slayer3D runtime viewport
- authored brush-world selection trace metadata
- data-authored world/selection/normal/hit debug overlay drawing
- reusable `ui.panels` and `ui.inspectors` populated from scene state
- a first blockout tool palette: `1` floor, `2` wall, `3` ceiling,
  `4` player start, then `Enter` to place the selected prefab
- unified editable-level export: `S` publishes one fragment containing the
  brush world and player starts
- generic runner test-run support: `--player-start player_start.editor_shell`
  enters the playable FPS scene through the same runtime path a data-only game
  uses

Run it with:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json
```

Test-run the authored player start directly:

```sh
./build/debug/slayer3d_runner --root demos/editor_shell_dojo/data --data asset://editor_shell_dojo.game.json --player-start player_start.editor_shell
```
