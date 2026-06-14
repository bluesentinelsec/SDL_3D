# Standalone Editor Packaging

`slayer3d_editor` is the desktop level editor host for Slayer3D maps. It is a
thin native executable that launches the data-authored editor shell through the
same managed runtime used by games.

## No-Argument Launch

Running the editor with no arguments starts a new untitled map using the bundled
Editor Shell Dojo project:

```sh
slayer3d_editor
```

The default save target is a non-conflicting `untitled*.slayermap.json` file in
SDL's per-user preferences directory for `bluesentinelsec/Slayer3DEditor`.
Explicit command-line launches remain available for project-specific workflows:

```sh
slayer3d_editor new --project path/to/project --output path/to/level.slayermap.json
slayer3d_editor open --project path/to/project --input path/to/level.slayermap.json
```

Set `SLAYER3D_EDITOR_PROJECT` to a project directory when testing another
project as the temporary no-argument default.

## Desktop Build

For a distributable desktop build, prefer static engine and SDL linkage where
the platform toolchain supports it:

```sh
cmake --preset default \
  -DBUILD_SHARED_LIBS=OFF \
  -DSLAYER3D_SDL_LINKAGE=static
cmake --build --preset default --target slayer3d_editor
```

The Windows MSVC static CI job verifies that `slayer3d_editor.exe` does not
import `SDL3.dll` when configured with static SDL linkage. macOS and Linux
package builds should use the same intent, but exact system library linkage is
toolchain and dependency-manager dependent.

## Install Layout

Native installs include:

- `bin/slayer3d_editor`
- `bin/slayer3d_runner`
- `bin/slayer3d_pack`
- `bin/slayer3d_bundle`
- `share/slayer3d/demos/editor_shell_dojo`
- `share/slayer3d/media`

The installed no-argument editor discovers the packaged Editor Shell Dojo at
`../share/slayer3d/demos/editor_shell_dojo` relative to the executable. Build
tree launches use the compiled-in source-tree default.

## Distribution Notes

The editor is generic. Project assets and map files stay external unless a
future package format embeds them. The bundled Editor Shell Dojo exists so a
fresh executable opens into a useful brush editor immediately; users can then
open or configure their own projects from the editor workflow.
