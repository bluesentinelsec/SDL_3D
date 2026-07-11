# Standalone Editor Packaging

`slayer3d_editor` is the desktop level editor host for Slayer3D maps. It is a
thin native executable that launches the data-authored editor shell through the
same managed runtime used by games.

## No-Argument Launch

Running the editor with no arguments starts a new untitled map using the bundled
Slayer3D Editor project:

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

## Skybox Authoring

The top-toolbar `Skybox` button opens a right-side Skybox panel that lists the
skyboxes discovered under the project's skybox asset source (the built-in
animated presets under `media/skyboxes/` for the repository editor project).
Selecting an entry and pressing `Apply` previews the sky in the viewport and
records it as the map's global sky, which is saved into the `.slayermap.json`
`skybox` block and restored on open. `Default` returns to the editor's
built-in sky. Skyboxes are discovered from the authoritative project media root,
normally `media/skyboxes/`. Nested subdirectories are scanned, and directories
are recognized as skyboxes when they contain six cubemap faces (`px.png` ...
`nz.png`) or animated layer images (`layer_outer.png`, optional
`layer_inner.png`).

## Bundled Assets

The default editor shell lives under `apps/slayer3d_editor/data` in source
builds and is launched as an explicit project root. Project media is not packed
or embedded into the native editor binary. When no `--media-path` is provided,
the editor looks for a `media/` directory in the current working directory; if
one is not available, the Project Media settings panel opens so the user can
choose a directory.

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
- `share/slayer3d/demos/slayermap_example`
- `share/slayer3d/media`

The installed no-argument editor needs access to the editor project data and a
media directory. Packaging should install or bundle the editor project data next
to the executable or in a platform-appropriate resource directory, and should
make a conventional `media/` directory available for first launch.

## Distribution Notes

The editor is generic. Project assets and map files stay external unless the
caller explicitly packages them. A fresh executable should open into a useful
brush editor immediately, but project media remains an explicit directory chosen
by CLI argument, current working directory convention, or the Project Media
settings panel.

## Browser Editor (Emscripten)

`slayer3d_editor_web` is an Emscripten-only CMake target that runs the same
data-authored editor shell in a web browser. The web build preloads
`apps/slayer3d_editor` and browser-test media into the Emscripten virtual
filesystem and resolves the default project from `/apps/slayer3d_editor` at
startup. No CLI arguments are required; the browser editor boots into the
default untitled map.

The supported build-and-host workflow is Docker-based:

```sh
docker build -f docker/emscripten-editor/Dockerfile -t slayer3d-editor-web .
docker run --rm -it -p 8080:8080 slayer3d-editor-web
```

Then open:

```text
http://localhost:8080/slayer3d_editor_web.html
```

With a local Emscripten SDK, the same target builds directly:

```sh
emcmake cmake -S . -B build/emscripten-editor -DCMAKE_BUILD_TYPE=Release
cmake --build build/emscripten-editor --target slayer3d_editor_web
python3 -m http.server 8080 --directory build/emscripten-editor
```

The build produces `slayer3d_editor_web.html`, `.js`, `.wasm`, and `.data`
artifacts in the build root. The managed game loop drives the browser build
through `emscripten_set_main_loop_arg` rather than the native blocking loop;
native desktop behavior is unchanged.

### Current Browser Limitations

- Saves write to the in-memory Emscripten filesystem, so edits do not persist
  across page reloads. Browser-native import/export (file picker and download
  bridge, or persistent IDBFS storage) is follow-up work.
- Open/save file dialogs depend on SDL's dialog support for Emscripten. When a
  dialog is unavailable, the editor reports "File dialog failed" in the editor
  console instead of failing silently.
