# Standard Options Package

Most games need the same baseline options screens: display, audio, keyboard,
mouse, and gamepad. SDL3D provides a `standard_options` scene package so a game
can reuse those screens while keeping game-specific choices in JSON.

The package is data-driven. The engine generates option scenes from
`scenes.standard_options`, and the game decides which actions are bindable, what
the scenes are named, which settings actor owns state, and which signals apply
or reset values.

## Adoption Checklist

1. Create a settings actor, usually `entity.settings`.
2. Add display, audio, and gamepad settings as actor properties.
3. Declare menu input actions and any gameplay actions that should be rebindable.
4. Add the `standard_options` package to `scenes.files`.
5. Configure `scenes.standard_options` with scene IDs, menu IDs, signals, and
   binding rows.
6. Add logic bindings for apply/reset signals.
7. Wire app window settings and audio bus volume to the same settings actor.

Pong is the reference implementation in
`demos/pong/data/pong.game.json`. A minimal loadable fixture is kept at
`tests/assets/game_data/standard_options_minimal.game.json`.

## Settings Actor

The reusable screens expect one actor to hold option state. Games may rename the
actor, but the default and recommended name is `entity.settings`.

```json
{
  "name": "entity.settings",
  "active": true,
  "tags": ["state", "settings"],
  "properties": {
    "display_mode": { "type": "string", "value": "windowed" },
    "vsync": { "type": "bool", "value": true },
    "renderer": { "type": "string", "value": "opengl" },
    "gamepad_icons": { "type": "string", "value": "xbox" },
    "vibration": { "type": "bool", "value": true },
    "sfx_volume": { "type": "int", "value": 8 },
    "music_volume": { "type": "int", "value": 7 }
  }
}
```

The standard property names are:

| Property | Type | Values |
| --- | --- | --- |
| `display_mode` | string | `windowed`, `fullscreen_exclusive`, `fullscreen_borderless` |
| `vsync` | bool | `true`, `false` |
| `renderer` | string | `software`, `opengl` |
| `gamepad_icons` | string | `xbox`, `nintendo`, `playstation` |
| `vibration` | bool | `true`, `false` |
| `sfx_volume` | int | `0` through `10` |
| `music_volume` | int | `0` through `10` |

Game-specific settings can live on the same actor or a different actor. The
standard package only reads and writes the properties above.

## Scene Package

Insert the package where the generated scenes should appear in scene order:

```json
{
  "scenes": {
    "initial": "scene.title",
    "files": [
      "scenes/title.scene.json",
      { "package": "standard_options" },
      "scenes/play.scene.json"
    ]
  }
}
```

Configure the package under `scenes.standard_options`:

```json
{
  "standard_options": {
    "settings": "entity.settings",
    "return_scene": "scene.title",
    "single_scene": true,
    "menu_state_key": "options_menu",
    "scenes": {
      "root": "scene.options",
      "display": "scene.options.display",
      "keyboard": "scene.options.keyboard",
      "mouse": "scene.options.mouse",
      "gamepad": "scene.options.gamepad",
      "audio": "scene.options.audio"
    },
    "menus": {
      "root": "menu.options",
      "display": "menu.options.display",
      "keyboard": "menu.options.keyboard",
      "mouse": "menu.options.mouse",
      "gamepad": "menu.options.gamepad",
      "audio": "menu.options.audio"
    },
    "actions": {
      "up": "action.menu.up",
      "down": "action.menu.down",
      "left": "action.menu.left",
      "right": "action.menu.right",
      "select": "action.menu.select"
    },
    "signals": {
      "move": "signal.ui.menu.move",
      "select": "signal.ui.menu.select",
      "apply": "signal.settings.apply",
      "apply_audio": "signal.settings.apply_audio",
      "reset_display": "signal.settings.reset_display",
      "reset_keyboard": "signal.settings.reset_keyboard",
      "reset_mouse": "signal.settings.reset_mouse",
      "reset_gamepad": "signal.settings.reset_gamepad",
      "reset_audio": "signal.settings.reset_audio"
    },
    "background": {
      "renders_world": true,
      "camera": "camera.overhead",
      "entities": [
        "entity.options.background.base",
        "entity.options.flow.magenta"
      ]
    }
  }
}
```

Omitted names fall back to the values shown above. Games should still author the
names explicitly once the project is more than a prototype; explicit IDs make
large projects easier to search and review.

`single_scene` is optional and defaults to `false`. When enabled, the root
options scene contains every standard options menu. Root menu items and child
Back items set `menu_state_key` in scene state instead of changing scenes, so
related submenus switch instantly and keep the same animated background,
particles, and lighting.

## Backgrounds

`background` is optional. If omitted, generated options scenes render no world
entities. If present, every generated options scene uses the same `renders_world`
value, camera, and entity filter. This is intended for reusable menu backdrops
such as particles, animated logo geometry, or authored lighting effects while
keeping unrelated gameplay entities out of options scenes.

## Rebindable Actions

Binding rows are game-owned. The engine only knows how to capture and apply the
new physical input. This keeps the standard screens reusable across Pong,
shooters, platformers, RPGs, and games with custom action names.

```json
{
  "bindings": {
    "keyboard": [
      {
        "label": "Up",
        "default": "UP",
        "bindings": [
          { "action": "action.player.up", "device": "keyboard" },
          { "action": "action.menu.up", "device": "keyboard" }
        ]
      }
    ],
    "mouse": [
      {
        "label": "Accept",
        "default": "LEFT",
        "bindings": [
          { "action": "action.menu.select", "device": "mouse" }
        ]
      }
    ],
    "gamepad": [
      {
        "label": "Accept",
        "default": "SOUTH",
        "bindings": [
          { "action": "action.menu.select", "device": "gamepad" }
        ]
      }
    ]
  }
}
```

Each row can update one or more actions. Use this when gameplay and menus share
a player-facing concept, such as `Up` controlling both a paddle and menu
navigation. Rows should be named by intent, not by device-specific input.

The standard rebinding UI captures keyboard keys, mouse buttons, and gamepad
buttons. Authored gameplay input also supports mouse axes and gamepad axes, but
axis rebinding needs game-specific policy for threshold, direction, and
conflicts.

## Apply And Reset

Standard controls apply immediately. There is no Apply/Cancel staging for the
default screens. Reset items emit signals, so games choose exactly what gets
reset and whether persistence should be updated.

```json
{
  "signal": "signal.settings.reset_audio",
  "actions": [
    {
      "type": "property.reset_defaults",
      "target": "entity.settings",
      "keys": ["sfx_volume", "music_volume"]
    },
    { "type": "signal.emit", "signal": "signal.settings.apply_audio" }
  ]
}
```

Window settings should be tied to `app.window.settings` so display changes can
be applied live:

```json
{
  "app": {
    "window": {
      "apply_signal": "signal.settings.apply",
      "apply_signals": ["signal.settings.apply", "signal.settings.reset_display"],
      "settings": {
        "target": "entity.settings",
        "display_mode": "display_mode",
        "vsync": "vsync",
        "renderer": "renderer"
      }
    }
  }
}
```

Audio settings use normal logic actions:

```json
{
  "type": "audio.set_bus_volume",
  "bus": "music",
  "source": { "target": "entity.settings", "key": "music_volume", "scale": 0.1 }
}
```

## Styling And Layout

The package has defaults, but games can override fonts, colors, cursor glyphs,
slider glyphs, alignment, and per-screen layout:

```json
{
  "fonts": {
    "title": "font.title",
    "menu": "font.hud"
  },
  "theme": {
    "title_color": [242, 248, 255, 255],
    "menu_color": [225, 236, 255, 245],
    "selected_color": [255, 245, 208, 255],
    "cursor_color": [255, 222, 140, 255],
    "status_color": [255, 222, 140, 230],
    "divider_color": [126, 168, 238, 170],
    "cursor": ">",
    "slider_left": "[",
    "slider_fill": "#",
    "slider_empty": "-",
    "slider_right": "]"
  },
  "layout": {
    "title_x": 0.5,
    "menu_x": 0.5,
    "status_x": 0.5,
    "status_y": 0.88,
    "menu_align": "left",
    "cursor_align": "right",
    "selected_pulse_alpha": true,
    "title_divider": true,
    "audio": { "title_y": 0.18, "menu_x": 0.34, "menu_y": 0.39, "gap": 0.078, "cursor_offset_x": -0.035 }
  }
}
```

Use custom authored scenes instead of the package when the structure itself is
different. Use package overrides when the structure is standard and only the
presentation or binding list changes.

## Extending The Menu Set

A game can add its own options scenes beside the standard package:

```json
{
  "scenes": {
    "files": [
      { "package": "standard_options" },
      "scenes/options_gameplay.scene.json"
    ]
  }
}
```

The custom scene should use the same settings actor and immediate-apply
patterns where possible. That keeps the player experience consistent while
allowing game-specific options such as difficulty, assist settings, reticles,
subtitles, or camera behavior.

## When Starting A New Game

Start from the minimal fixture, then make these project-specific edits:

1. Rename metadata, scene IDs, menu IDs, and storage metadata.
2. Replace binding rows with the actions your game exposes.
3. Keep display/audio/gamepad properties on the settings actor unless the game
   has a strong reason to split them.
4. Add persistence once the option set is stable.
5. Override theme/layout after the menu behavior is correct.
