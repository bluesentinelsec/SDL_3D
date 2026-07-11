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

## Styling

Nodes may author:

- `color` or `fill_color`: background fill color.
- `border_color` and `border_thickness`: optional border.
- `font`, `text` or `label`: static text content.
- `text_color`, `text_scale`, and `align`: text presentation.

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
control even though the popup is a flat command. When `selected_value_key` is a
numeric scene-state value, author numeric `values`.

## Visibility And Selection

`visible_if` and `selected_if` conditions read the same data sources used by UI
bindings. This keeps toolbars and menus data-driven: the same authored widget
can show, hide, or switch styling based on scene state without C-side editor or
game-specific logic.
