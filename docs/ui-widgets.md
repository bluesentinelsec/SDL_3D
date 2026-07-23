# Retained UI Widgets

`ui.widgets` is the preferred authored UI model for game HUDs, menus, tools,
and editor shells. Widgets are retained nodes: authored as a tree, resolved into
logical-pixel rectangles each frame, then emitted as flat render and hit-test
commands. Rendering, hit testing, dropdown popups, and dynamic text therefore
share one layout source of truth.

Use `ui.rects`, `ui.text`, and `ui.images` for small one-off overlays. Treat
`ui.panels` and `ui.inspectors` as legacy/simple helpers for diagnostics. New
structured UI should use `ui.widgets` so controls can move without child
geometry drifting away from their parents.

## Authoring Rules

These rules exist because hand-maintained absolute coordinates caused repeated
regressions (thumbnails drifting out of their slots, stale clip rectangles,
labels detaching from moved panels):

1. Author widgets that visually belong together as one component: a slot cell
   is a `button` whose thumbnail `image` and labels are its `children`.
2. Never author panel content in `ui.images` / `ui.rects` with absolute
   coordinates. Content belongs in the widget tree, where position, layer,
   clipping, and visibility are inherited from the parent.
3. Do not duplicate ancestor `visible_if` conditions on children. A child is
   only shown when every ancestor is visible, so author each condition once,
   on the node that owns it.
4. Do not author `clip_rect` values that mirror another node's geometry. Set
   `clip_children` on the container; descendants inherit the clip.
5. Omit `layer` on children when possible — a child automatically resolves
   one layer above its parent.

## Layout

Widget coordinates are logical pixels. The renderer scales the final UI stream
to the active display while the 3D/world layer can use independent render
scaling.

Each node has an `id`, a `type`, optional `x` / `y`, and a size. `w` / `h` may
be a positive number or `"fill"`. Children are positioned relative to their
parent. Containers can use `layout: "row"`, `layout: "column"`, or
`layout: "grid"` with optional `padding` and `gap`.

Supported node types are:

- `panel`, `toolbar`, `row`, `column`, and `spacer` for structure.
- `button`, `dropdown`, and `tab_strip` for interactive controls.
- `label`, `console`, and `log_view` for text output.
- `image` for thumbnails and icons.
- `scroll` for vertically scrolling panes.

Interactive retained controls receive hover and active visual states by
default. Buttons in an authored panel use the same hover feedback as
synthesized dropdown options; menu authors do not add per-button hover state
or pointer-coordinate conditions.

### Scroll Panes

`scroll` containers own their children the way a Tk or GTK scrolled window
does. Children are authored in the pane's content coordinates; the pane always
clips them, measures the content extent from their resolved rectangles,
clamps the requested offset into `[0, extent - viewport]`, shifts the whole
subtree, and synthesizes a proportional scrollbar whenever content overflows.
Nothing scroll-related is authored per child, and there is no authored
maximum anywhere — rows hidden by `visible_if` (a collapsed section, an
inactive tab) shrink the extent and the scrollbar follows automatically.

### Virtualized Lists

Fixed-slot browsers (six texture cells windowing hundreds of files) scroll by
rebinding slot contents per index, not by moving pixels. Author `scroll_key`
plus `scroll_count_key` on the container that holds the slots: it becomes a
virtualized list that scrolls in item units. The container's resolved
children are the visible window, `scroll_max` is `count - visible`, and the
same proportional scrollbar is synthesized as for pixel scroll panes - one
scrollbar implementation everywhere. `scroll_signal` names the signal hosts
emit after a scrollbar drag changes the index so the slots rebind.

```json
{
  "id": "ui.browser.grid",
  "type": "panel",
  "layout": "grid",
  "columns": 2,
  "scroll_key": "editor.texture.scroll.index",
  "scroll_count_key": "editor.texture.count",
  "scroll_signal": "signal.editor.texture.filter",
  "children": [ "...six slot cells..." ]
}
```

`scroll_key` names the scene-state float that owns the offset. Steppers and
data-authored actions adjust that key freely; the host calls
`slayer3d_game_data_sync_ui_scroll_limits()` once per update to pull the key
back inside the measured bounds and to publish `<scroll_key>.limit` for
anything that wants to bind against the real maximum. Scrollbar drags map
pointer positions through the pane's resolved geometry via
`slayer3d_ui_layout_scrollbar_offset_for_pointer()`, so no C code carries
thumb or travel math.

```json
{
  "id": "ui.panel.rows",
  "type": "scroll",
  "x": 0, "y": 94, "w": 296, "h": 318,
  "scroll_key": "editor.inspector.scroll",
  "children": [
    { "id": "ui.panel.rows.first", "type": "panel", "x": 12, "y": 0, "w": 272, "h": 26 },
    { "id": "ui.panel.rows.second", "type": "panel", "x": 12, "y": 30, "w": 272, "h": 26 }
  ]
}
```

### Grid Containers

`layout: "grid"` places children row-major into a fixed number of `columns`.
Cell width is the padded content width divided by the column count (minus
gaps); children with `w: "fill"` stretch to the cell, fixed-size children keep
their size at the cell origin. Each row is as tall as its tallest fixed-height
child. Use grids for asset browsers, palettes, and forms:

```json
{
  "id": "ui.browser.grid",
  "type": "panel",
  "layout": "grid",
  "columns": 2,
  "w": 218, "h": 390,
  "padding": 10,
  "gap": 12,
  "clip_children": true,
  "children": [
    {
      "id": "ui.browser.cell0",
      "type": "button",
      "w": 78, "h": 78,
      "action": "editor.asset.select.0",
      "children": [
        { "id": "ui.browser.cell0.thumbnail", "type": "image", "image": "image.asset.slot_0", "x": 8, "y": 8, "w": 62, "h": 62 }
      ]
    }
  ]
}
```

### Images

`image` nodes draw an image asset (`image`) inside the node rect and take part
in layout like any other node: they inherit parent position, clipping,
visibility, and layering, so a thumbnail authored as a child of its slot
button can never drift out of the slot. Set `preserve_aspect: true` to fit the
source aspect ratio inside the rect, and an optional `color` to tint. Images
are display-only unless they author `interactive` / `action`.

### Edge Anchoring

`anchor_x: "right"` / `anchor_y: "bottom"` reinterpret a node's `x` / `y` as
margins from the opposite edge of its parent (or the viewport, for roots).
Docked panels author their margin once and track any viewport size, so a
right-docked browser never encodes the canonical resolution in its position.

### Movable And Docked Windows

A root widget may author a `window` object. `drag_handle` names an interactive
descendant that begins pointer capture, while `x_key` / `y_key` own the
floating position and `dock_key` owns `"none"`, `"left"`, `"right"`, or
`"bottom"`. Side-docked windows fill the vertical canvas area above the
bottom dock stack and stack horizontally inward in authored order.
Bottom-docked windows fill the available width and stack upward. Optional
`dock_width` and `dock_height` values override a window's floating dimension
on the corresponding dock axis. Window roots paint an opaque surface by
default and form stacking contexts, so one window's descendants cannot bleed
through a window above it.

The retained layout marks the named descendant as a semantic drag handle.
That role makes the full handle interactive without requiring editor-specific
hit tests. A `drag_indicator` widget placed anywhere inside the same window
automatically follows that handle's hover and pressed state. It renders the
standard compact three-bar grip, keeping visual feedback bounded while the
larger title bar remains easy to grab.

```json
{
  "id": "ui.inspector.panel",
  "type": "panel",
  "x": 12, "y": 92, "x_key": "editor.inspector.x", "y_key": "editor.inspector.y",
  "w": 308, "h": 500,
  "interactive": true,
  "window": {
    "drag_handle": "ui.inspector.header",
    "dock_key": "editor.inspector.dock",
    "front_key": "editor.ui.window.front",
    "default_dock": "left",
    "dock_top": 80,
    "dock_gap": 4,
    "snap_distance": 48,
    "drag_threshold": 4,
    "titlebar_visible_width": 48
  }
}
```

`front_key` names a scene-state string containing the id of the front-most
root window. The host writes the clicked or opened window id to this key. The
layout then raises that window's complete subtree above every other root
window while preserving the relative order of its own controls. If no visible
window matches the key, the last visible root window is the fallback front
window.

Window roots and drag/resize handles must be interactive. The host keeps a
header press pending until `drag_threshold` is crossed, then undocks the
window and captures the drag until button release. Floating bounds keep the
drag handle vertically reachable and retain at least
`titlebar_visible_width` horizontal pixels, while allowing a large window's
body to extend beyond the viewport. Releasing within `snap_distance` of the
left, right, or bottom edge docks the window on that side. The host writes
only authored state keys and consumes all pointer input within the resolved
window ancestry. Empty panel space and text inputs therefore cannot leak
clicks, wheel events, or drags to the world canvas.

Editors should render non-interactive feedback from the active drag state:
edge target rails while dragging, an outline around a floating result, and
the resolved dock slot when an edge is targeted. The preview should match
the same authored ordering and gap rules used by final dock layout.

For a bottom console, use the same window contract with
`default_dock: "bottom"`. Author `h_key` on the root and a top-edge
`resize_handle`, `resize_edge: "top"`, `height_key`, and min/max heights.
`resolved_height_key` can publish the actual height for virtualized content
when a side dock stretches the window. Visibility remains ordinary
data-authored state.

### Wheel And Scrollbar Behavior

Every scrollable container - pixel `scroll` panes and virtualized lists -
shares one interaction model: the synthesized scrollbar drags through the
pane's resolved geometry, wheel input over the pane adjusts its `scroll_key`
(one item per notch for lists, 40px for panes, overridable with
`scroll_step`), and events over panes are consumed before the world/canvas.
Clicks outside an open dropdown popup dismiss it and are likewise consumed.
Transient panels and menus can author `outside_click_action`; the retained
layout synthesizes a full-viewport hit region immediately behind the node.
Clicks inside still reach the node or its children, while clicks outside emit
the authored action and cannot propagate to the canvas.

Bare-key editor shortcuts gate on the single scene fact
`editor.ui.text_entry.active`, published by the runtime from the live
text-field focus keys before input sensors evaluate; new text fields extend
the C-side computation instead of adding conditions to every sensor.

## Styling

Retained text uses semantic theme roles. Widgets default to `body`; author
`text_role` as `caption` or `heading` only when the content has that semantic
purpose. Role sizes are logical pixels, so display density improves glyph
atlas sharpness without changing layout size.

```json
{
  "ui": {
    "typography": {
      "body": { "size": 14, "color": [215, 224, 238, 245] },
      "caption": { "size": 12, "color": [157, 171, 190, 235] },
      "heading": { "size": 14, "color": [238, 244, 252, 255] }
    },
    "widgets": [
      {
        "id": "ui.inspector.title",
        "type": "label",
        "text": "Inspector",
        "text_role": "heading",
        "w": 120,
        "h": 24
      }
    ]
  }
}
```

Nodes may author:

- `color` or `fill_color`: background fill color.
- `border_color` and `border_thickness`: optional border.
- `font`, `text` or `label`: static text content.
- `text_role`: `body` (the default), `caption`, or `heading`.
- `text_color`: an explicit per-node override of the role color.
- `text_scale`: a legacy explicit scale override for exceptional content.
- `align`: `left`, `center`, or `right`.

Colors are `[r, g, b]` or `[r, g, b, a]` byte arrays. `align` may be `left`,
`center`, or `right`.

## Dynamic Text

Dynamic text uses `format` plus `bindings`. The number of `%s` placeholders in
`format` must exactly match the number of bindings. Bindings can read
`scene_state`, actor `property`, or runtime `metric` values.

For a single numeric scene-state value, use `text_format` plus `text_value_key`.
This is useful for compact editor controls such as a grid-size dropdown label.

```json
{
  "id": "ui.hud.fps",
  "type": "label",
  "font": "font.hud",
  "format": "FPS %s",
  "bindings": [
    { "type": "metric", "metric": "fps", "default": 0 }
  ],
  "x": 1100,
  "y": 16,
  "w": 160,
  "h": 30,
  "align": "right"
}
```

## Dropdowns

Dropdowns author `options`, optional `values`, `open_key`, and
`selected_value_key`. The layout resolver emits the dropdown button plus
synthesized popup and option commands. The popup and options use the dropdown as
their `owner_id`, so input dispatch and rendering stay attached to the authored
control even though the popup is a flat command. `values` may contain strings,
booleans, integers, or floats; the selected option follows the matching typed
scene-state value named by `selected_value_key`.

## Visibility And Selection

`visible_if` and `selected_if` conditions read the same data sources used by UI
bindings. This keeps toolbars and menus data-driven: the same authored widget
can show, hide, or switch styling based on scene state without C-side editor or
game-specific logic.
