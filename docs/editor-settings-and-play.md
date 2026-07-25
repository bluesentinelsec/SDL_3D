# Editor Settings And Play Mode

`File > Settings` opens a retained floating child window. It is draggable,
front-raising, opaque, clipped, resizable from its bottom-right grip, and
intentionally non-dockable. Closing it releases text focus and any open
dropdown. Settings are persisted under the editor's user storage profile.

The window currently owns:

- Display mode: windowed, fullscreen borderless, or fullscreen exclusive.
- Project media directory, using the folder contract in
  `editor-media-directory.md`.
- Map runner selection: the built-in play mode or an external program.

The window mode used to launch the editor initializes the display setting when
no persisted preference exists. This preserves the platform default, including
windowed web builds. Applying settings updates the live window through the
normal data-authored app-window settings path.

## Built-In Play

The editor's Play button and F5 command save the current map before starting.
An unnamed document opens Save As; cancellation or a failed save does not
enter play mode.

Built-in play runs inside `slayer3d_editor`; it does not launch another process
and does not require a shared library or script runtime. If the map contains an
editor actor with:

```json
{
  "actor-type": "player-character"
}
```

the built-in runner spawns the first-person controller at that actor's position
and orientation. Movement uses WASD, mouse look, and Space to jump, with
brush-world collision.

When no player-character actor exists, play starts from the editor perspective
camera in collision-free fly mode. WASD moves horizontally, Q/E moves
vertically, Shift increases speed, and the mouse controls the view. Escape
returns to the editor in either mode.

## External Programs

External mode accepts an executable path and an argument string. An absolute
path is used directly; a relative path is resolved from the editor project
directory. The editor starts the process without a shell, so arguments are
tokenized into an argv array and shell expansion is not performed.

The only supported substitution is `{current_map}`. It expands to the absolute
path of the successfully saved map:

```text
--map "{current_map}"
```

External programs are unavailable in web builds. The built-in runner remains
the default and the first-class single-executable workflow.
