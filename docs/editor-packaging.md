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

The desktop editor starts in exclusive fullscreen mode by default. Use the
launch-only `--window-mode` option to compare presentation modes without
changing project data:

```sh
slayer3d_editor --window-mode fullscreen
slayer3d_editor --window-mode windowed
slayer3d_editor --window-mode fullscreen-borderless
```

`fullscreen` selects an exclusive display mode, while
`fullscreen-borderless` uses the desktop display mode in a borderless window.
The File menu's `Exit` command requests the managed app shutdown path, allowing
normal shutdown callbacks and engine cleanup to complete.

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
are recognized as skyboxes when they contain a seamless equirectangular
`sphere.png` panorama. Legacy six-face cubemap directories (`px.png` ...
`nz.png`) and animated layer directories (`layer_outer.png`, optional
`layer_inner.png`) remain readable for old content.
See [Skybox Asset Contract](skybox-assets.md) for the project media contract.

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
cmake --preset editor \
  -DBUILD_SHARED_LIBS=OFF \
  -DSLAYER3D_SDL_LINKAGE=static
cmake --build --preset editor
```

The `editor` preset is a Release build scoped to the interactive editor. The
`default` preset is intentionally an unoptimized Debug build for engine
development. Debug builds should remain interactive, but their frame cost is not
representative of a distributed editor build.

The Windows MSVC static CI job verifies that `slayer3d_editor.exe` does not
import `SDL3.dll` when configured with static SDL linkage. macOS and Linux
package builds should use the same intent, but exact system library linkage is
toolchain and dependency-manager dependent.

## Frame Profiling

Set `SLAYER3D_PROFILE_FRAMES=1` before launching the editor to log averaged
frame-stage timings once per second. On PowerShell:

```powershell
$env:SLAYER3D_PROFILE_FRAMES = "1"
build/editor/slayer3d_editor.exe
```

Each sample reports FPS and milliseconds spent polling events, running fixed
ticks, submitting render work, and presenting. Presentation, UI collection, and
managed render-wrapper samples further divide the render cost. The managed-loop
sample also reports frame-time percentiles, missed target frame budgets (60 FPS
by default), hitch counts, and the stage breakdown for the slowest frame in each
interval. Compare profiles from the same map and window size, and include the
build preset: Debug and optimized timings are not directly comparable.

On macOS, build the symbolized optimized editor and record an interactive Time
Profiler session with:

```sh
cmake --preset editor-profile
cmake --build --preset editor-profile
tools/profile_editor_macos.sh
```

The launcher enables frame profiling, captures terminal output, and attaches
Instruments to the editor. Closing the editor finalizes a timestamped directory
under `build/profiles` containing `editor.log`, `time-profile.trace`, and build
metadata. Pass normal editor arguments after the script name when needed.

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

The web editor defaults to windowed canvas presentation. Browser fullscreen
requires a user gesture, so it is not requested automatically during startup;
the desktop editor continues to default to exclusive fullscreen.

### Current Browser Limitations

- Saves write to the in-memory Emscripten filesystem, so edits do not persist
  across page reloads. Browser-native import/export (file picker and download
  bridge, or persistent IDBFS storage) is follow-up work.
- Open/save file dialogs depend on SDL's dialog support for Emscripten. When a
  dialog is unavailable, the editor reports "File dialog failed" in the editor
  console instead of failing silently.
