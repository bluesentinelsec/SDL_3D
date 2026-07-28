# Slayer3D Editor Hotkeys

This document lists every input binding configured by
`apps/slayer3d_editor/data/slayer3d_editor.game.json`.

`Primary` means Command on Apple platforms and Control on other platforms.
Unless a table says otherwise, a shortcut is exact: holding an additional
modifier prevents it from firing. Editor shortcuts are disabled while a text
input has focus.

## File

| Action | Shortcut |
| --- | --- |
| New document | `Primary+N` |
| Open document | `Primary+O` |
| Save document | `Primary+S` |
| Save document as | `Primary+Shift+S` |
| Open Settings | `Primary+,` |
| Play the current map | `F5` |

## Edit

| Action | Shortcut |
| --- | --- |
| Undo | `Primary+Z` |
| Redo | `Primary+Shift+Z` |
| Redo alternate | `Ctrl+Y` |
| Cut | `Primary+X` |
| Copy | `Primary+C` |
| Paste | `Primary+V` |
| Delete selection | `Delete` or `Backspace` |
| Duplicate selection | `Primary+D` |
| Flip selection horizontally | `Shift+X` |
| Flip selection vertically | `Shift+Y` |

## Tools

The number-row keys select tools. Numeric keypad keys are intentionally
unassigned so they remain available for future viewport orientation controls.

| Tool | Shortcut |
| --- | --- |
| Select | `Space` |
| Brush | `1` |
| Edge | `2` |
| Clip | `3` |
| Face | `4` |
| Vertex | `5` |
| Rotate | `6` |
| Scale | `7` |
| Shear | `8` |
| Texture | `9` |
| Open Things | `G` |
| Open Inspector | `I` |

## Tool Operations

| Action | Shortcut | Context |
| --- | --- | --- |
| Commit current operation | `Enter` | Editor tool operation |
| Cancel current operation or return to Select | `Escape` | Editor |
| Cycle Clip keep mode | `Primary+Enter` | Clip tool |
| Lower brush | `[` | Brush editing |
| Raise brush | `]` | Brush editing |
| Toggle wall axis | `R` | Wall brush placement |
| Decrease grid size | `-` or keypad `-` | Editor |
| Increase grid size | `=` or keypad `+` | Editor |

## Perspective Viewport

`W/A/S/D/Q/E` are reserved for canvas movement. They allow Shift for fast
movement, but Control, Command, and Alt prevent movement so application
commands cannot move the camera.

| Action | Input |
| --- | --- |
| Move forward | `W` |
| Move backward | `S` |
| Move left | `A` |
| Move right | `D` |
| Move up | `Q` |
| Move down | `E` |
| Fast movement | hold `Left Shift` |
| Mouse look | hold right mouse button and move |
| Mouse pan | hold middle mouse button and move |
| Frame selected object | `Primary+U` |

## Orthographic Viewports

These bindings operate on the active orthographic viewport.

| Action | Input |
| --- | --- |
| Pan left/right | `Left` / `Right` |
| Pan up/down | `Up` / `Down` |
| Zoom in | `Z` |
| Zoom out | `X` |
| Zoom | mouse wheel |

## Built-In Runner

Press `Escape` to return to the editor.

When a map supplies a player character:

| Action | Shortcut |
| --- | --- |
| Move | `W/A/S/D` |
| Jump | `Space` |
| Look | mouse movement |

When the built-in fly camera is used:

| Action | Shortcut |
| --- | --- |
| Move | `W/A/S/D` |
| Move up/down | `Q/E` |
| Fast movement | hold `Left Shift` |
| Look | mouse movement |
