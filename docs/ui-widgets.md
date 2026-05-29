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

## Layout

Widget coordinates are logical pixels. The renderer scales the final UI stream
to the active display while the 3D/world layer can use independent render
scaling.

Each node has an `id`, a `type`, optional `x` / `y`, and a size. `w` / `h` may
be a positive number or `"fill"`. Children are positioned relative to their
parent. Containers can use `layout: "row"` or `layout: "column"` with optional
`padding` and `gap`.

Supported node types are:

- `panel`, `toolbar`, `row`, `column`, and `spacer` for structure.
- `button`, `dropdown`, and `tab_strip` for interactive controls.
- `label`, `console`, and `log_view` for text output.

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
