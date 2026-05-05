#SDL3D Game Data JSON Format

This document defines the first JSON-family authoring format for SDL3D game data. It is intentionally small, explicit, and module-oriented so it can express Pong without assuming sectors, while still leaving room for Doom's FPS/sector modules.

The format is data-first: game rules should primarily be expressed as entities, components, sensors, signals, timers, conditions, and actions. Game-specific code should appear as named adapters only where a rule is not genuinely reusable. For a function-level Lua reference, see [docs/game-data-lua.md](game-data-lua.md).

## Format Goals

- Represent the generic model from `docs/game-data-model.md`.
- Keep sectors, tiles, rooms, and 3D scenes optional world modules.
- Prefer named references over runtime numeric ids.
- Let loaders validate missing targets, duplicate names, invalid action payloads, and unsupported module requirements before gameplay starts.
- Keep engine-owned primitives declarative and inspectable.
- Reserve game adapters for specialized math or policy, not ordinary wiring.

## Top-Level Object

Every file is a JSON object with these fields:

| Field | Required | Purpose |
| --- | --- | --- |
| `schema` | yes | Schema id. First value: `sdl3d.game.v0`. |
| `metadata` | yes | Human-readable identity and versioning. |
| `storage` | no | Writable storage identity used to resolve `user://` and `cache://` roots. |
| `persistence` | no | Reusable actor-property persistence entries for settings, saves, and profiles. |
| `app` | no | Managed-loop startup config such as title, logical resolution, window mode, backend, tick rate, and tick cap. |
| `profiles` | no | Genre/profile hints and required modules. |
| `assets` | no | Stable asset ids mapped to paths or future archive refs. |
| `scripts` | no | Lua scripts that provide game-specific behavior used by adapters. |
| `input` | no | Named actions and bindings. |
| `world` | yes | World identity, coordinate policy, bounds, cameras, lights. |
| `render` | no | Renderer setup such as clear color, lighting, bloom, SSAO, and tonemapping. |
| `transitions` | no | Named screen transition descriptors such as startup and quit fades. |
| `ui` | no | Authored UI descriptors. |
| `entities` | yes | Stable named actors with tags, transforms, properties, and components. |
| `signals` | no | Authored signal names. |
| `logic` | no | Sensors, timers, bindings, conditions, and actions. |
| `adapters` | no | Named game-specific extension points used by logic actions or controllers. |

## Naming

Names are authored handles and should be stable across reloads. Use role namespaces:

- `entity.ball`
- `camera.main`
- `signal.score.player`
- `logic.goal.cpu`
- `timer.round.serve`
- `adapter.pong.reflect_ball`

Loaders should reject duplicate names inside a namespace and should report the JSON pointer of the failure.

## Storage

The optional `storage` block declares the platform-stable identity used by
`sdl3d_storage_create()`:

```json
{
    "storage" : {"organization" : "Blue Sentinel Security", "application" : "SDL3D Pong", "profile" : "default"}
}
```

`organization` and `application` should be stable after release because they
become part of the writable save/settings/cache paths on every platform.
`profile` is optional and scopes roots below `profiles/<profile>`. Test tools
may also author `user_root_override` and `cache_root_override`, but shipped
games should normally let SDL3D choose platform-idiomatic locations.

## Persistence

The optional `persistence` block declares reusable save files for actor
properties. Each entry owns one storage path and an allowlist of properties from
one actor. Logic can load or save the entry without game-specific Lua:

```json
{
    "persistence":
    {
        "entries" : [ {
            "name" : "persistence.options",
            "path" : "user://settings/options.json",
            "schema" : "sdl3d.options.v1",
            "version" : 1,
            "target" : "entity.settings",
            "enabled_if" : {
                "type" : "property.bool",
                "target" : "entity.settings",
                "key" : "options_persistence_enabled",
                "equals" : true
            },
            "properties" : [ "display_mode", "vsync", "renderer", "sfx_volume", "music_volume" ]
        } ]
    }
}
```

    The generated JSON stores `schema` and `version` when authored,
    then writes the allowlisted properties at the top level.Load ignores missing files and incompatible schema /
        version files,
    which lets games reset defaults or
        migrate old data explicitly. `enabled_if` uses the same condition language as UI and logic,
    so games can disable persistence during development or per profile
                                                               .

                                                           Persistence actions are regular logic actions :

```
    json{"type" : "persistence.load",
         "entry" : "persistence.options"} {"type" : "persistence.save", "entry" : "persistence.options"}
```

    ##World

        The world object declares one spatial context.It does not imply a sector map.

```json
{
    "name" : "world.pong",
             "kind" : "fixed_screen",
                      "units" : "world_units",
                                "axes" : {"horizontal" : "x", "vertical" : "y", "depth" : "z"},
                                         "bounds" : {"min" : [ -9.0, -5.0, -1.0 ], "max" : [ 9.0, 5.0, 2.0 ]},
                                                    "cameras" : [],
                                                                "lights" : []
}
```

Future world kinds can include `tile_grid`, `room_graph`, `sector_map`, and `scene_3d`.

## App And Presentation

The optional `app` object lets a managed-loop game declare startup settings before a window exists:

```json
{
    "app":
    {
        "title": "SDL3D Pong",
    "logical_width": 1280,
    "logical_height": 720,
    "backend": "software",
    "icon_path": "media/icons/game.png",
    "settings_path": "user://settings/options.json",
    "window": {
      "display_mode": "fullscreen_borderless",
      "development_display_mode": "windowed",
      "production_display_mode": "fullscreen_borderless",
      "maximized": true,
      "resizable": true,
      "vsync": true,
      "renderer": "software",
      "apply_signal": "signal.settings.apply",
      "apply_signals": ["signal.settings.apply", "signal.settings.reset_display"],
      "settings": {
        "target": "entity.settings",
        "display_mode": "display_mode",
        "renderer": "renderer",
        "vsync": "vsync"
      }
    },
    "tick_rate": 0.008333333,
    "max_ticks_per_frame": 12,
    "start_signal": "signal.game.start",
    "pause": {
      "action": "action.pause",
      "allowed_if": {
        "type": "property.compare",
        "target": "entity.match",
        "key": "finished",
        "op": "==",
        "value": false
      }
    },
    "startup_transition": "startup",
    "quit": {
      "action": "action.exit",
      "transition": "quit",
      "quit_signal": "signal.app.quit_fade_done"
    },
    "scene_shortcuts": [
      { "action": "action.scene.title", "scene": "scene.title" },
      { "action": "action.scene.play", "scene": "scene.play" }
    ],
    "input_policy": {
      "global_actions": ["action.exit", "action.scene.title", "action.scene.play"]
    },
    "scene_transition_policy":
        {
            "allow_same_scene" : false, "allow_interrupt" : false, "reset_menu_input_on_request" : true
        }
    }
}
```

The optional `render`, `transitions`, and `ui` sections describe presentation without giving the data
runtime ownership of the renderer. Games read these descriptors and decide how to apply them.

`start_signal` lets data kick off initial timers or scripted startup logic after the host has loaded the runtime.
`app.logical_width` and `app.logical_height` define the game's virtual resolution; desktop window size and fullscreen
presentation are policy, not game-world scale. `app.window` can author display mode, renderer, vsync, title/icon
overrides, and development/production display-mode defaults. `settings_path`, when present, lets startup read persisted
display options such as `display_mode`, `renderer`, and `vsync` before creating the window.
`app.window.apply_signal`, when present, lets the reusable app-flow helper apply live window settings from the
configured settings actor after a menu emits that signal. `app.window.apply_signals` can list additional signals,
such as menu-specific reset signals, that should trigger the same live apply. The helper applies display mode and
V-sync in place and recreates the window/render context when the renderer backend changes.
`app.pause.action` and `startup_transition` let the managed-loop host avoid hardcoded action/transition names.
`app.pause.allowed_if` is optional; when present, the app-flow helper evaluates it before entering pause.
`app.quit` lets data choose which input action requests quit, which transition plays, and which signal completes
the quit. `app.scene_shortcuts` maps authored input actions to scene names, which is useful for development
hotkeys, debug level jumps, and simple game flows. `app.input_policy.global_actions`
lists actions that remain valid in every scene, while each scene can further
limit its own actions. `app.scene_transition_policy` controls whether scene
requests can re-enter the current scene, interrupt an active transition, and
reset menu input arming after accepted requests. The host still owns process shutdown.

The optional `update_phases` and `presentation` sections let thin managed-loop
hosts use generic frame orchestration:

```json
{
    "update_phases" : {
        "app_flow" : {"active" : true, "when_paused" : true},
        "presentation" : {"active" : true, "when_paused" : true},
        "property_effects" : {"active" : true, "when_paused" : true},
        "particles" : {"active" : true, "when_paused" : true},
        "simulation" : {"active" : true, "when_paused" : false}
    },
                      "presentation":
    {
        "metrics" : {"fps_sample_seconds" : 0.25}, "ui_pulse_clock" : "clock.pause_flash", "clocks" : [ {
            "name" : "clock.pause_flash",
            "target" : "entity.presentation",
            "key" : "pause_flash",
            "speed_property" : {"target" : "entity.presentation", "key" : "pause_flash_speed"},
            "wrap" : 1.0,
            "reset_on_pause_enter" : true,
            "active_if" : {"type" : "app.paused", "equals" : true}
        } ]
    }
}
```

`sdl3d_game_data_update_frame()` uses these phases to advance app flow,
    presentation clocks, property effects, particles,
    and simulation.Presentation clocks are reusable oscillators / counters that can write into actor properties;
UI, lights, scripts,
    and render helpers can all read the resulting value
            .

        Font assets can be authored under `assets.fonts`:

```json
{
    "assets":
    {
        "fonts" : [ {"id" : "font.hud", "builtin" : "Inter", "size" : 34} ]
    }
}
```

    UI descriptors can bind text to engine metrics or
    actor properties and can use generic visibility conditions :

```json
{
    "name": "ui.score",
  "font": "font.hud",
  "format": "%02d   %02d",
  "bindings": [
    { "type": "property", "entity": "entity.score.player", "key": "value" },
    { "type": "property", "entity": "entity.score.cpu", "key": "value" }
  ],
  "visible_if":
    {
        "type" : "not", "condition":
        {
            "type" : "app.paused", "equals" : true
        }
    }
}
```

Supported UI binding sources are `metric` (`fps`, `frame`, `paused`), `property`,
and `scene_state`.
Supported UI conditions include `always`, `app.paused`, `camera.active`, `property.compare`,
`property.bool`, `scene_state.compare`, `all`, `any`, and `not`.
`scene_state.compare` may author `default` as a fallback scalar when the
requested scene-state key has not been set yet.

Scene UI can also declare data-driven menu presenters. A presenter turns a
scene `menus[]` controller into a visible stack with authored alignment, colors,
selected styling, and cursor styling:

```json
{
    "ui":
    {
        "menus" : [ {
            "name" : "ui.title.menu",
            "menu" : "menu.title",
            "font" : "font.hud",
            "x" : 0.5,
            "y" : 0.48,
            "gap" : 0.09,
            "normalized" : true,
            "align" : "center",
            "color" : [ 225, 236, 255, 245 ],
            "selected_color" : [ 255, 245, 208, 255 ],
            "cursor" : {"text" : ">", "offset_x" : -0.12, "color" : [ 255, 222, 140, 255 ]}
        } ]
    }
}
```

Menu items can set scene state instead of changing scenes. This is useful for
submenus that should share one scene, camera, background, and transition state:

```json
{
    "label" : "Display", "scene_state" : {"key" : "options_menu", "value" : "display"}
}
```

This keeps title/options/menu screens data-authored: the input menu defines
choices and actions, while the UI presenter defines how the choices are laid out.
Scene UI images may also author a generic `effect` and `effect_speed`. The
first supported effect is `melt`, which is applied during overlay rendering on
both software and GL backends. `effect_speed` controls how quickly the effect
advances in presentation time. Because the effect is driven by the reusable
overlay pipeline, it works for any authored UI image, including sprite-backed
logos and menu art.
Menu items can also author generic settings controls and emit data-authored signals when a value changes:

```json
{
    "label" : "Difficulty", "signal" : "signal.settings.apply", "control":
    {
        "type" : "choice", "target" : "entity.settings", "key" : "difficulty", "choices" : [
            {"label" : "Easy", "value" : "easy"}, {"label" : "Normal", "value" : "normal"},
            {"label" : "Hard", "value" : "hard"}
        ]
    }
}
```

Supported control types are `toggle`, `choice`, `range`, `input_binding`, and
`text`.
The generic menu controller applies property controls directly to actor
properties and the menu UI presenter displays the current value. A menu can
author `left_action` and `right_action` so range and choice controls can be
adjusted without activating navigation items:

```json
{
    "name" : "menu.options.audio",
             "up_action" : "action.menu.up",
                           "down_action" : "action.menu.down",
                                           "left_action" : "action.menu.left",
                                                           "right_action" : "action.menu.right",
                                                                            "select_action" : "action.menu.select",
                                                                                              "items"
        : [ {
            "label" : "Music",
            "signal" : "signal.settings.apply_audio",
            "control" : {
                "type" : "range",
                "target" : "entity.settings",
                "key" : "music_volume",
                "value_type" : "int",
                "display" : "slider",
                "min" : 0,
                "max" : 10,
                "step" : 1,
                "default" : 7
            }
        } ]
}
```

    Range controls clamp to their authored min /
    max.Adding `"value_type" : "int"` stores the result as an integer property; adding `"display": "slider"` renders
the menu value as a compact slider label.

`text` controls capture editable UTF-8 text and write it to scene state or an
actor string property. While a text control is focused, text input, Backspace,
Delete, Return, Escape, and the menu select/back actions are consumed by the
text editor instead of leaking into parent menu navigation. This is the
preferred primitive for forms such as direct-connect hostnames, save names,
profile names, and chat-style short fields:

```json
{
    "label" : "Host",
    "control" : {
        "type" : "text",
        "target" : "scene_state",
        "key" : "direct_connect_host",
        "default" : "127.0.0.1",
        "placeholder" : "Host / IP",
        "charset" : "hostname",
        "max_length" : 64
    }
}
```

Omit `target` or set it to `"scene_state"` to bind the value to persistent
scene state. Set `target` to an entity id to bind an actor string property.
Supported `charset` values are `text`, `utf8`, `ascii`, `integer`, `digits`,
`numeric`, and `hostname`. `hostname` accepts letters, digits, `.`, `-`, `_`,
and `:` so it can store hostnames, IPv4 addresses, IPv6-style text, and ports.
Restricted charsets reject non-ASCII UTF-8 input. `max_length` is measured in
UTF-8 bytes and must be 255 or fewer.

Menus can also include a data-authored dynamic list item. A dynamic list expands
one authored menu row into zero or more selectable rows at runtime. The first
source type is `scene_state_indexed`, which reads a count and indexed label /
value keys from scene state. This is useful for server browsers, save-slot
lists, profile selectors, inventory rows, and similar UI where the row count is
not known when the scene JSON is authored:

```json
{
    "type" : "dynamic_list",
    "name" : "list.local_matches",
    "source" : {
        "type" : "scene_state_indexed",
        "count_key" : "local_match_count",
        "label_key_format" : "local_match_%d_label",
        "value_key_format" : "local_match_%d_endpoint"
    },
    "empty_label" : "No local matches found",
    "label_format" : "Join {label}",
    "selected_index_key" : "selected_local_match_index",
    "selected_value_key" : "selected_local_match_endpoint",
    "scene_state" : { "key" : "selected_local_match", "value_from" : "value" },
    "signal" : "signal.multiplayer.join_selected"
}
```

The menu controller treats expanded rows like normal menu items: Up/Down moves
through them, Back still resolves the authored back item, and Accept selects the
current row. If the source count is zero, `empty_label` renders one inert row
with no signal, scene change, or scene-state mutation. `selected_index_key` and
`selected_value_key` are optional scene-state outputs updated as the highlighted
row moves. `scene_state.value_from` may be `value`, `label`, or `index` and is
applied when a populated row is accepted. UI menu presenters may author
`visible_count` to show a scrolling window around the selected row instead of
rendering every row.

`input_binding` controls capture the next keyboard key, mouse button, or
gamepad button and immediately rebind all authored actions for that device. This is how games can
expose one player-facing input such as `Up` while updating both gameplay and
menu actions:

```json
{
    "label" : "Up", "control":
    {
        "type" : "input_binding", "default" : "UP", "bindings" : [
            {"action" : "action.paddle.up", "device" : "keyboard"}, {"action" : "action.menu.up", "device" : "keyboard"}
        ]
    }
}
```

    For gamepads,
    use the same control shape with `"device"
    : "gamepad"` and a button name such
          as `"SOUTH"` or `"DPAD_UP"`.Duplicate inputs within the same menu are rejected during capture.Pressing the
                              capture cancel key,
    Escape by default,
    cancels capture.Resetting authored binding controls is a logic action :

```json{"type" : "input.reset_bindings", "menu" : "menu.options.keyboard"}
```

    Settings menus that need Apply /
        Cancel behavior can still snapshot the edited properties when a scene opens,
    let controls stage values in those actor properties,
    then either persist the staged values or restore the snapshot :

```json
{
    "type" : "property.snapshot",
             "name" : "options.display",
                      "target" : "entity.settings",
                                 "keys" : [ "display_mode", "vsync", "renderer" ]
}
```

```json{"type" : "property.restore_snapshot", "name" : "options.display", "target" : "entity.settings"}
```

    Settings screens can reset authored defaults without game -
    specific code :

```json
{
    "type" : "property.reset_defaults", "target" : "entity.settings", "keys" : [ "display_mode", "vsync", "renderer" ]
}
```

### Standard Options Contract

Games that want SDL3D's reusable options behavior should author one settings
actor and bind option controls to that actor. The actor name is game-defined,
but `entity.settings` is the recommended default because it matches the app
window and audio examples.

For a project-oriented adoption checklist and a minimal reference game file,
see [docs/standard-options.md](standard-options.md).
For a concise new-game checklist that mirrors the Pong template structure,
see [docs/pong-template-checklist.md](pong-template-checklist.md).

Standard display properties:

| Property | Type | Expected values |
| --- | --- | --- |
| `display_mode` | string | `windowed`, `fullscreen_exclusive`, `fullscreen_borderless` |
| `vsync` | bool | `true` or `false` |
| `renderer` | string | `software` or `opengl` |

Standard audio properties:

| Property | Type | Expected values |
| --- | --- | --- |
| `sfx_volume` | int | Usually `0` to `10`; use `scale: 0.1` for bus volume |
| `music_volume` | int | Usually `0` to `10`; use `scale: 0.1` for bus volume |

Standard gamepad properties:

| Property | Type | Expected values |
| --- | --- | --- |
| `gamepad_icons` | string | `xbox`, `nintendo`, `playstation` |
| `vibration` | bool | `true` or `false` |

Keyboard, mouse, and gamepad configuration should be authored as action-oriented
`input_binding` menu controls. Games decide which actions to expose, but each
control should represent a player-facing intent such as `Up`, `Accept`,
`Cancel`, or `Pause`, not a low-level device-specific operation. The same
player-facing item may update multiple engine actions, such as gameplay
movement and menu navigation.

Gameplay input bindings may also use authored mouse axes and gamepad axes. The
standard rebinding UI captures button-like inputs; analog axis rebinding needs
game-specific policy for threshold, direction, and conflict behavior.

Runtime input role/device policy can be authored under `input.profiles`.
Profiles let a scene or adapter select a complete binding overlay without
hard-coding device assignment in host C. Applying a profile first unbinds every
action named in `unbind`, then applies either raw authored bindings or reusable
device assignments. A single profile may use `bindings` or `assignments`, but
not both; keeping those styles separate avoids ambiguous ordering and keeps
profiles easy to audit.

```json
{
  "input": {
    "device_assignment_sets": [
      {
        "name": "assignment.keyboard.vertical_arrows",
        "device": "keyboard",
        "bindings": [
          { "semantic": "up", "key": "UP" },
          { "semantic": "down", "key": "DOWN" }
        ]
      },
      {
        "name": "assignment.gamepad.vertical_dpad_left_stick",
        "device": "gamepad",
        "bindings": [
          { "semantic": "up", "button": "DPAD_UP" },
          { "semantic": "down", "button": "DPAD_DOWN" },
          { "semantic": "up", "axis": "left_y", "scale": -1.0 },
          { "semantic": "down", "axis": "left_y", "scale": 1.0 }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.play.lan.client",
        "active_if": {
          "type": "all",
          "conditions": [
            { "type": "scene_state.compare", "key": "match_mode", "op": "==", "value": "lan" },
            { "type": "scene_state.compare", "key": "network_role", "op": "==", "value": "client" }
          ]
        },
        "min_gamepads": 0,
        "max_gamepads": 4,
        "unbind": [
          "action.player1.up",
          "action.player2.up"
        ],
        "assignments": [
          {
            "set": "assignment.keyboard.vertical_arrows",
            "actions": { "up": "action.player2.up", "down": "action.player2.down" }
          },
          {
            "set": "assignment.gamepad.vertical_dpad_left_stick",
            "slot": 0,
            "actions": { "up": "action.player2.up", "down": "action.player2.down" }
          }
        ]
      }
    ]
  }
}
```

`active_if` uses the standard data condition language. If omitted, the profile
matches any scene state and can act as an explicit fallback. `min_gamepads` and
`max_gamepads` are optional gates evaluated against the live input manager; an
omitted `max_gamepads` means no upper bound. Gamepad bindings may omit `slot` or
use `-1` to accept any connected gamepad; otherwise `slot` pins the binding to a
local player index. Authors should keep profile predicates mutually exclusive or
order them from most specific to most general, because
`sdl3d_game_data_apply_active_input_profile()` applies the first matching
profile.

Device assignment sets define reusable device-specific bindings once, using
semantic names such as `up`, `down`, `accept`, or `fire`. A profile assignment
maps those semantics to concrete game actions and may pin gamepad bindings to a
slot. This supports common policies like keyboard-as-player-1,
first-gamepad-as-player-2, first-two-gamepads-as-player-1/player-2, and
LAN-client controls without duplicating key/button/axis rows in every profile.
Every key in an assignment `actions` map must match a semantic declared by the
referenced set, and every semantic in the set must be mapped to an action.
`slot` is valid only when the referenced assignment set has `"device": "gamepad"`;
keyboard and mouse sets are global device policies and reject slot-specific
assignments.

Logic bindings can apply profiles directly:

- `{ "type": "input.apply_active_profile" }` applies the first profile whose
  authored conditions and gamepad gates match the current scene state.
- `{ "type": "input.apply_profile", "profile": "profile.name" }` applies one
  named profile.
- `{ "type": "input.clear_network_input_overrides", "channel":
  "client_input" }` clears action overrides written by an authored
  `client_to_host` network input channel, preventing stale remote input from
  leaking into later local play.

Use an adapter or data actions before `input.apply_active_profile` when a game
needs to normalize scene-state values that choose the active profile.
For hotplug support, hosts can keep a
`sdl3d_game_data_input_profile_refresh_state` and call
`sdl3d_game_data_apply_active_input_profile_on_device_change()` from their
frame loop while a profile-managed scene is active. The helper applies the
active profile on first use and after the connected gamepad count changes,
leaving scene-entry profile application in authored data actions.

Reusable options scenes should prefer immediate apply for settings where the
player benefits from real-time feedback. Use Apply/Cancel snapshots only for
screens that intentionally stage changes before committing them.

The `standard_options` scene package generates root Options, Display, Keyboard,
Mouse, Gamepad, and Audio scenes from reusable engine templates. The game controls the
scene ids, menu ids, settings actor, signals, input actions, fonts, theme
colors, and player-facing input binding rows:

```json
{
    "scenes":
    {
        "files" : [ "scenes/title.scene.json", {"package" : "standard_options"}, "scenes/play.scene.json" ],
                  "standard_options":
        {
            "settings" : "entity.settings",
                         "return_scene" : "scene.title",
                                          "single_scene" : true,
                                                           "menu_state_key" : "options_menu",
                                                                              "scenes" : {
                                                                                  "root" : "scene.options",
                                                                                  "display" : "scene.options.display",
                                                                                  "keyboard" : "scene.options.keyboard",
                                                                                  "mouse" : "scene.options.mouse",
                                                                                  "gamepad" : "scene.options.gamepad",
                                                                                  "audio" : "scene.options.audio"
                                                                              },
                                                                                         "actions"
                : {
                    "up" : "action.menu.up",
                    "down" : "action.menu.down",
                    "left" : "action.menu.left",
                    "right" : "action.menu.right",
                    "select" : "action.menu.select"
                },
                  "signals" : {
                      "move" : "signal.ui.menu.move",
                      "select" : "signal.ui.menu.select",
                      "apply" : "signal.settings.apply",
                      "apply_audio" : "signal.settings.apply_audio",
                      "reset_display" : "signal.settings.reset_display",
                      "reset_keyboard" : "signal.settings.reset_keyboard",
                      "reset_mouse" : "signal.settings.reset_mouse",
                      "reset_gamepad" : "signal.settings.reset_gamepad",
                      "reset_audio" : "signal.settings.reset_audio"
                  },
                              "fonts" : {"title" : "font.title", "menu" : "font.hud"},
                                        "background"
                : {
                    "renders_world" : true,
                    "camera" : "camera.overhead",
                    "entities" : [ "entity.options.background.base", "entity.options.flow.magenta" ]
                },
                  "theme" : {
                      "title_color" : [ 242, 248, 255, 255 ],
                      "menu_color" : [ 225, 236, 255, 245 ],
                      "selected_color" : [ 255, 245, 208, 255 ],
                      "cursor_color" : [ 255, 222, 140, 255 ],
                      "status_color" : [ 255, 222, 140, 230 ],
                      "divider_color" : [ 126, 168, 238, 170 ],
                      "cursor" : ">",
                      "slider_left" : "[",
                      "slider_fill" : "#",
                      "slider_empty" : "-",
                      "slider_right" : "]"
                  },
                            "layout"
                : {
                    "title_x" : 0.5,
                    "menu_x" : 0.5,
                    "status_x" : 0.5,
                    "status_y" : 0.88,
                    "menu_align" : "left",
                    "cursor_align" : "right",
                    "selected_pulse_alpha" : true,
                    "title_divider" : true,
                    "root" :
                        {"title_y" : 0.18, "menu_x" : 0.43, "menu_y" : 0.36, "gap" : 0.078, "cursor_offset_x" : -0.035},
                    "display" :
                        {"title_y" : 0.2, "menu_x" : 0.3, "menu_y" : 0.38, "gap" : 0.074, "cursor_offset_x" : -0.035},
                    "keyboard" :
                        {"title_y" : 0.13, "menu_x" : 0.36, "menu_y" : 0.29, "gap" : 0.062, "cursor_offset_x" : -0.035},
                    "mouse" :
                        {"title_y" : 0.13, "menu_x" : 0.4, "menu_y" : 0.30, "gap" : 0.062, "cursor_offset_x" : -0.035},
                    "gamepad" : {
                        "title_y" : 0.105,
                        "menu_x" : 0.34,
                        "menu_y" : 0.24,
                        "gap" : 0.055,
                        "cursor_offset_x" : -0.035
                    },
                    "audio" :
                        {"title_y" : 0.18, "menu_x" : 0.34, "menu_y" : 0.39, "gap" : 0.078, "cursor_offset_x" : -0.035}
                },
                  "bindings":
            {
                "keyboard" : [ {
                    "label" : "Up",
                    "default" : "UP",
                    "bindings" : [
                        {"action" : "action.player.up", "device" : "keyboard"},
                        {"action" : "action.menu.up", "device" : "keyboard"}
                    ]
                } ],
                             "mouse" : [ {
                                 "label" : "Accept",
                                 "default" : "LEFT",
                                 "bindings" : [ {"action" : "action.menu.select", "device" : "mouse"} ]
                             } ],
                                       "gamepad" : [ {
                                           "label" : "Up",
                                           "default" : "DPAD_UP",
                                           "bindings" : [
                                               {"action" : "action.player.up", "device" : "gamepad"},
                                               {"action" : "action.menu.up", "device" : "gamepad"}
                                           ]
                                       } ]
            }
        }
    }
}
```

    The package owns common menu composition and standard setting controls; the game
owns the bindings and names that make sense for its rules. `theme` controls
colors, cursor text, and slider glyphs. `layout` controls normalized title,
menu, status, row gap, cursor offset, alignment, and selected-item pulse values
for each generated scene. Games can omit `theme` or `layout` fields to use sane
defaults, or author fully custom options scenes when they need a different
structure. `background` is optional;
when present,
    generated options scenes render the listed entities with the named camera
        .This gives every standard options submenu the same authored backdrop while keeping gameplay actors out of the
            menu scene. `single_scene` makes root and child options menus live inside the root scene;
child menu selections set `menu_state_key` in scene state instead of requesting scene
        transitions.Use it when a family of menus should feel like one screen
            .

    Audio bus volume can also be driven from actor properties so options menus can share one generic settings
        actor.Use `source.scale` when the authored setting uses player -
    facing integer units rather than normalized engine volume :

```json
{
    "type" : "audio.set_bus_volume",
             "bus" : "music",
                     "source" : {"target" : "entity.settings", "key" : "music_volume", "scale" : 0.1}
}
```

Haptics policies can be authored under `haptics.policies`. Each policy names the
trigger signal, rumble intensity, and duration. Hosts can enumerate policies
with `sdl3d_game_data_haptics_policy_count()` and
`sdl3d_game_data_get_haptics_policy_at()`, subscribe to the referenced signals,
then call `sdl3d_game_data_match_haptics_policy()` for each signal payload before
starting rumble.

```json
{
  "haptics": {
    "policies": [
      {
        "name": "haptics.player_hit",
        "signal": "signal.player.hit",
        "enabled_if": {
          "type": "property.compare",
          "target": "entity.settings",
          "key": "vibration",
          "op": "==",
          "value": true
        },
        "low_frequency": 0.45,
        "high_frequency": 0.75,
        "duration_ms": 120,
        "payload_actor_filters": [
          { "key": "other_actor_name", "tags": ["player"] }
        ]
      }
    ]
  }
}
```

`enabled_if` is optional and uses the same condition syntax as UI visibility and
profile activation. `payload_actor_filters` is optional; when present, at least
one filter must match. A filter reads an actor name from the signal payload field
named by `key`, then matches either a concrete `actor` reference or all authored
`tags` on that actor. Filters may also have their own `active_if` condition.
Intensities must be numbers from 0 to 1, and `duration_ms` must be a positive
integer.

    Sprite assets can be authored under `assets.sprites`:

```json
{
    "assets":
    {
        "sprites" : [ {
            "id" : "sprite.robot.walk",
            "path" : "asset://sprites/robot/walk.png",
            "frame_width" : 32,
            "frame_height" : 48,
            "columns" : 8,
            "rows" : 6,
            "frame_count" : 6,
            "direction_count" : 8,
            "fps" : 8.0,
            "loop" : true,
            "lighting" : true,
            "emissive" : false,
            "visual_ground_offset" : 0.125,
            "effect" : "melt",
            "effect_delay" : 1.0,
            "effect_duration" : 1.0
        } ]
    }
}
```

    The current sprite metadata API records the image source,
    atlas / grid layout, animation timing, directional frame count, lighting participation,
    an optional overlay effect plus timing window, and optional sprite shader source paths for the GL backend.
    Callers can use `sdl3d_game_data_load_sprite_asset()` to resolve the asset through the runtime's resolver and
    build billboard sprites, 2D-in-3D sheets, or directional animation sets without changing the authored JSON shape.
    If `shader_vertex_path` and `shader_fragment_path` are provided, the runtime loads the shader source text once,
    caches it with the sprite, and the GL renderer compiles it the first time that source pair is drawn. The software
    renderer ignores authored shader sources and falls back to the normal sprite or overlay path.

    The load path decodes the source once and keeps the runtime-owned textures alive until `sdl3d_sprite_asset_free()`.

    Image assets under `assets.images` may also point at a sprite asset through a `sprite` field. The UI presentation
    layer will realize that sprite into a cached texture and reuse the same sprite runtime path instead of a separate
    image-only loader. If the sprite asset authors an overlay effect such as `melt`, the UI image inherits that effect
    timing from the sprite metadata. Authored sprite shader sources follow the same rule: the UI image inherits the
    sprite's GL shader sources when present, and the renderer compiles each distinct source pair once and reuses the
    program for later draws.

    Performance note: sprite sheets are decoded and sliced during load, not per frame. For large sprite families,
    prefer a single authored sheet over many tiny source files so the loader does one decode and one texture
    allocation pass instead of repeated file-system reads. Shader source files are also loaded once per sprite asset
    and compiled lazily on first draw in the GL backend. Reuse shared shader files across sprites when you can, so the
    engine pays the compile cost only once per distinct source pair.

        ##Scenes

        Games can split authored content across many scene files.The top
        -
        level game file declares the initial scene and the scene files to load :

```json
{
    "scenes":
    {
        "initial" : "scene.title", "files" : [ "scenes/title.scene.json", "scenes/level_001.scene.json" ]
    }
}
```

    Entries in `scenes.files` are normally scene -
    file paths.A game may also insert an engine -
    provided scene package at a specific point in the scene order :

```json
{
    "scenes":
    {
        "initial" : "scene.title",
                    "files" : [ "scenes/title.scene.json", {"package" : "standard_options"}, "scenes/play.scene.json" ]
    }
}
```

    Each scene file is a JSON object with schema `sdl3d.scene.v0`.Scene files may declare whether gameplay updates,
    whether the authored world renders, which entities belong to the scene, which camera becomes active on entry,
    scene enter / exit transitions, menus,
    and scene - local UI :

```json
{
    "schema" : "sdl3d.scene.v0",
               "name" : "scene.level_001",
                        "updates_game" : true,
                                         "renders_world" : true,
                                                           "camera" : "camera.main",
                                                                      "on_enter_signal" : "signal.level.enter",
                                                                                          "entities"
        : [ "entity.player", "entity.goal" ],
          "input" : {"actions" : [ "action.move.left", "action.move.right", "action.pause" ]},
                    "transitions" : {"enter" : "scene_in", "exit" : "scene_out"}
}
```

    Scenes that omit `entities` include all top -
    level entities; scenes with an empty
`entities` array include none. This keeps small demos terse while letting large
games isolate title screens, menus, levels, dungeons, and cutscenes.
Scenes that omit `input.actions` accept every action except where app-level
policy says otherwise. Scenes that author `input.actions` accept only those
actions plus app-level `global_actions`.

When a scene is activated, the runtime emits the scene's `on_enter_signal` if
one is authored. The enter payload always includes `from_scene` and `to_scene`.
Callers may pass additional transient payload properties with
`sdl3d_game_data_set_active_scene_with_payload()`.

For state that must survive after a transition, use the runtime scene-state
property bag exposed by `sdl3d_game_data_mutable_scene_state()`. Lua adapters
can access the same persistent bag through `ctx:state_get(key, fallback)` and
`ctx:state_set(key, value)`.

The optional `game_presentation` helper API can run common app and scene flow
directly from data. `sdl3d_game_data_app_flow_update()` consumes authored quit,
pause, menu, and scene-shortcut actions, while `sdl3d_game_data_scene_flow_request()`
starts the active scene's authored `exit` transition, switches scenes when the
transition finishes, then starts the target scene's authored `enter` transition.
Hosts that need custom loading screens or streaming can still orchestrate scene
changes themselves.

`sdl3d_game_data_update_frame()` is the higher-level update companion for thin
managed-loop games. It evaluates authored update phases, advances app flow,
presentation clocks, property effects, particle emitters, and simulation using
the same scene/input policies as the lower-level helpers.

The same helper module can draw authored `render.cube`, `render.sphere`, UI
text descriptors, and active `particles.emitter` components for the active
scene. `sdl3d_game_data_draw_frame()` applies authored render settings, lights,
camera, world primitives, particles, UI text, and transitions in the usual order,
with hooks for custom game rendering. Lower-level helpers remain available for
hosts that need a custom frame graph.

Authored menus can also be updated independently through
`sdl3d_game_data_update_menus()`. The helper handles input arming, movement, and
selected-item resolution, then returns a data command for the host or app-flow
controller to apply. This keeps title screens, options screens, and simple scene
menus from growing bespoke per-game input code.

Scenes may author an `activity` block for attract-mode and kiosk-style
behavior. The activity controller emits `on_enter`, `on_idle`, `on_active`, and
`periodic` action arrays. Set `consume_wake_input` to `true` when the first
input after an idle fade should only reveal the UI instead of also activating
menus, scene shortcuts, or pause:

```json
{
    "activity": {
        "enabled": true,
        "input": "any",
        "idle_after": 5.0,
        "consume_wake_input": true,
        "block_menus_on_wake": true,
        "block_scene_shortcuts_on_wake": true,
        "on_idle": [
            {"type": "ui.animate", "target": "ui.title.menu", "property": "alpha", "to": 0.0, "duration": 0.65}
        ],
        "on_active": [
            {"type": "ui.animate", "target": "ui.title.menu", "property": "alpha", "to": 1.0, "duration": 0.20}
        ]
    }
}
```

## Entities

Entities are the data form of actor registry entries plus optional component ownership.

```json
{
    "name" : "entity.ball",
             "active" : true,
                        "tags" : [ "ball", "dynamic" ],
                                 "transform" : {"position" : [ 0, 0, 0.12 ]},
                                               "properties" : {"velocity" : {"type" : "vec2", "value" : [ 0, 0 ]}},
                                                              "components"
        : [ {"type" : "render.sphere", "radius" : 0.22}, {"type" : "collision.circle", "radius" : 0.22} ]
}
```

Core fields are generic. Components are module-owned and may be ignored by
runtimes that do not load that module.

### Presentation Components

The first generic presentation components are deliberately simple descriptors:

- `render.cube` describes an axis-aligned box with `size`, optional `offset`,
  `color`, and `emissive`.
- `render.sphere` describes a sphere with `radius`, `rings`, `slices`, `color`,
  optional `texture`, `rotation_axis`, `rotation_angle`, `rotation_property`,
  and `emissive`.
- `particles.emitter` describes an emitter config that can be passed to the
  particle system.

The game data runtime exposes these as read-only descriptors. It does not issue
draw calls itself; each game or renderer decides how to render, sort, tint, or
override them. The optional `game_presentation` helpers provide a default
implementation for simple demos and tools.

Render primitives may also author generic visual effects:

```json
{
    "type" : "render.sphere", "radius" : 0.22, "color" : [ 255, 184, 82, 255 ], "lighting" : true, "effects" : [
        {
            "type" : "pulse",
            "rate" : 9.0,
            "color" : [ 255, 245, 156, 255 ],
            "radius_add" : 0.06,
            "emissive_base" : [ 0.75, 0.48, 0.08 ],
            "emissive_add" : [ 0.65, 0.30, 0.0 ]
        },
        {"type" : "drift", "offset" : [ 0.4, 0.2, 0.0 ], "rates" : [ 0.7, 0.5, 0.0 ], "phase" : 1.2}
    ]
}
```

Set `lighting` to `false` on a render primitive for solid-color unlit geometry
such as menu backplates, sky cards, debug overlays, and other presentation
surfaces that should not be affected by world lights.

Sphere primitive `texture` values reference `assets.images` ids. The default
presentation helper applies the texture as lit albedo when the sphere has
lighting enabled. `rotation_axis` and `rotation_angle` rotate the sphere locally
at draw time; `rotation_property` adds a float actor property to the authored
angle, which lets data-authored motion components animate a textured sphere
without game-specific C.

Supported primitive effects are:

- `flash`: reads a float property from a source entity and uses it to blend color, add size, and add emissive color.
- `pulse`: uses presentation time to animate color, emissive color, optional `size_add`, and optional `radius_add`.
- `drift`: uses presentation time to offset primitive position with sinusoidal motion. Use `offset`, `rates`, and optional `phase`.
- `emissive`: adds a constant emissive color.

Particle emitters may include `draw_emissive` for host renderers that draw particles through emissive lighting.

World lights may also declare `effects`. `pulse` effects can blend color and
add intensity/range over time; `color_cycle` effects smoothly interpolate
through an authored palette; `flash` effects read a float actor property and use
it as the effect weight. This lets data tune glows, brightness, color shifts,
and transient flashes without host code.

```json
{
    "name" : "light.ball",
             "type" : "point",
                      "target_entity" : "entity.ball",
                                        "color" : [ 1.0, 0.86, 0.34 ],
                                                  "intensity" : 3.1,
                                                  "range" : 5.0,
                                                  "effects"
        : [
            {"type" : "pulse", "rate" : 9.0, "color" : [ 1.0, 0.96, 0.54 ], "intensity_add" : 0.9},
            {"type" : "color_cycle", "duration" : 8.0, "colors" : [[1.0, 0.1, 0.1], [1.0, 0.45, 0.1], [0.9, 0.1, 0.5]]},
            {"type" : "flash", "source" : "entity.presentation", "property" : "paddle_flash", "intensity_add" : 1.5}
        ]
}
```

        ## #Cameras

        Cameras can be static perspective /
        orthographic descriptors or
    generic behavior descriptors. `type : "chase"` follows an entity using its velocity property,
    authored distance, height, lookahead,
    and offsets :

```json{
        "name" : "camera.ball_chase",
        "type" : "chase",
        "target_entity" : "entity.ball",
        "velocity_property" : "velocity",
        "chase_distance" : 2.6,
        "height" : 1.57,
        "lookahead" : 4.4,
        "target_offset" : [ 0.0, 0.0, 0.22 ],
        "fovy" : 68.0
    }
```

    ##Logic

        Logic is event composition :

    -**Sensors **observe world /
                entity state and emit signals.-
            **Timers **emit signals later or
        repeatedly.- **Bindings **attach ordered actions to signals.- **Conditions **gate actions.-
            **Actions **mutate generic state,
    emit signals, start timers, or invoke adapters.

                                        ## #Sensors

                                        Example :

```json{
            "name" : "logic.goal.cpu",
            "type" : "sensor.bounds_exit",
            "entity" : "entity.ball",
            "axis" : "x",
            "side" : "min",
            "threshold" : -9.22,
            "on_enter" : "signal.goal.cpu"
                                        }
```

    Sensors should be reusable and non - sector unless their type explicitly names a sector module,
    such as `sensor.sector_enter`
            .

        ## #Actions

        Generic action types should cover common wiring :

    - `property
            .set` - `property
                        .add` - `property
                                    .toggle` - `property
                                                   .snapshot` - `property
                                                                    .restore_snapshot` - `entity
                                                                                             .set_active` with `target`
                                                                                                 and boolean `active`,
    used to include or
        exclude an entity from generic updates and
                rendering - `transform
                                .set_position` - `motion
                                                     .set_velocity` - `signal
                                                                          .emit` - `timer
                                                                                       .start` - `timer
                                                                                                     .cancel` - `camera
                                                                                                                    .set_active` - `effect
                                                                                                                                       .set_active` - `audio
                                                                                                                                                          .play_sound` - `audio
                                                                                                                                                                             .crossfade_ambient` - `adapter
                                                                                                                                                                                                       .invoke`

                                                                                                                                                                                                   Actions run
                                                                                                                                                                                                       in array
                                                                                                                                                                                                           order
                                                                                                                                                                                                       .Conditions may
                                                                                                                                                                                                           be embedded
                                                                                                                                                                                                               per action
                                                                                                                                                                                                       .

```json
{
    "signal" : "signal.score.player", "actions" : [
        {"type" : "property.add", "target" : "entity.score.player", "key" : "value", "value" : 1},
        {"type" : "transform.set_position", "target" : "entity.ball", "position" : [ 0, 0, 0.12 ]},
        {"type" : "timer.start", "timer" : "timer.round.serve", "delay" : 1.0, "signal" : "signal.ball.serve"}
    ]
}
```

    Presentation state that changes continuously can be expressed with components instead of host
        code. `property.decay` moves an integer or
    float actor property toward a target value at an authored rate :

```json
{
    "name" : "entity.presentation",
             "properties" : {
                 "border_flash" : {"type" : "float", "value" : 0.0},
                 "border_flash_decay" : {"type" : "float", "value" : 2.8}
             },
                            "components" : [ {
                                "type" : "property.decay",
                                "property" : "border_flash",
                                "rate_property" : "border_flash_decay",
                                "target" : 0.0,
                                "min" : 0.0,
                                "max" : 1.0
                            } ]
}
```

`motion.velocity_2d` integrates an actor's vec2/vec3 velocity property into
its transform each update. `motion.spin` increments a float actor property by
`rate` radians per second, defaulting to the property `rotation_angle`. It is
useful for driving render primitive `rotation_property` values. `motion.oscillate`
moves an actor along an authored sinusoid and is useful for data-authored lamps,
platforms, attract-mode props, and other looping presentation motion:

```json
{
    "type": "motion.oscillate",
    "origin": [0.0, 3.8, 0.34],
    "amplitude": [6.8, 0.0, 0.0],
    "rate": 0.45,
    "phase": -1.5707963
}
```

The runtime stores oscillator time on the actor property named by
`time_property`, defaulting to `motion_time`. Set the actor's `active_motion`
property to `false` to pause generic motion components.

    ## #Conditions

    Conditions should be generic comparisons over properties,
    signal payloads, tags,
    and entity active state.

```json
{
    "if" : {"type" : "property.compare", "target" : "entity.score.player", "key" : "value", "op" : ">=", "value" : 10},
           "then" : [ {"type" : "signal.emit", "signal" : "signal.match.player_won"} ]
}
```

## Adapters

Adapters are named extension points implemented either by native C callbacks registered at runtime or by Lua functions declared in JSON. They should be narrow and inspectable.

Lua-backed adapters are loaded from top-level scripts:

```json
"scripts": [
  {
    "id": "script.pong",
    "path": "scripts/pong.lua",
    "module": "pong.rules",
    "dependencies": [],
    "autoload": true
  }
],
"adapters": [
  {
    "name": "adapter.pong.reflect_from_paddle",
    "kind": "action",
    "script": "script.pong",
    "function": "reflect_from_paddle"
  }
]
```

The loader resolves script paths relative to the JSON file, validates script ids/modules/dependencies, loads modules in deterministic dependency order, and requires each Lua module file to return a table. Adapter functions are resolved from that module table at load time and stored as Lua registry references, so per-frame adapter calls do not perform dotted global string lookup.

Lua modules should avoid global namespace ownership. A typical module returns a table:

```lua
local rules = {}

function rules.reflect_from_paddle(ball, payload, ctx)
  local paddle = ctx:actor(payload.other_actor_name)
  if paddle == nil then
    return false
  end

  -- game-specific rule code
  ball.velocity = Vec3(7.0, 1.5, 0.0)
  return true
end

return rules
```

Lua adapters receive `(target, payload, ctx)`:

- `target` is an actor wrapper for the authored action/component target, or `nil`.
- `payload` is a table copied from the signal payload or component payload.
- `ctx` provides adapter context: `ctx.adapter`, `ctx.dt`, `ctx:actor(name)`,
  `ctx:actor_with_tags(...)`, `ctx:state_get(key, fallback)`,
  `ctx:state_set(key, value)`, `ctx:random()`, `ctx:log(message)`, and
  `ctx.storage` safe access to `user://` and `cache://` paths.

Use exact actor names when they are stable. `ctx:actor_with_tags(...)` performs a registry scan and is best reserved for authored role lookup, scene setup, and other non-hot-path rules.

The preferred Lua API is intentionally game-script oriented. Scripts can check `sdl3d.api`, currently `sdl3d.lua.v1`, when they need to guard version-specific behavior:

```lua
local speed = ball:get_float("base_speed", 5.6)
ball.position = Vec3(0, 0, 0.12)
ball.velocity = Vec3.normalize(ball.velocity) * speed
```

Actor wrappers expose property helpers (`get_float`, `set_float`, `get_int`, `set_int`, `get_bool`, `set_bool`, `get_string`, `set_string`, `get_vec3`, `set_vec3`) plus `position` and `velocity` convenience fields. Missing actor names return `nil` from `sdl3d.actor(name)` and from `get_position()` / `get_vec3()`. `Vec3` provides `new`, `length`, `normalize`, `clamp`, and arithmetic operators. `math.clamp` and `math.lerp` are also available. The lower-level `sdl3d.get_*` and `sdl3d.set_*` functions remain available as implementation primitives, but gameplay scripts should prefer the wrapper API.

Scripts can persist structured data through `sdl3d.json.decode(text)` and
`sdl3d.json.encode(value)`. The JSON bridge accepts Lua booleans, numbers,
strings, nil, array-like tables, and string-keyed object tables. It is intended
for save files, settings, high scores, and other small gameplay data blobs.
Decode returns `nil, error_message` on invalid JSON. Encode returns
`nil, error_message` when a table is cyclic, too deeply nested, or uses
non-string object keys:

```lua
local scores = sdl3d.json.decode(ctx.storage.read("user://scores/pong.json") or "{}")
scores.player_wins = (scores.player_wins or 0) + 1
ctx.storage.write("user://scores/pong.json", sdl3d.json.encode(scores))
```

Native C registration remains available for host applications that need engine-facing integrations or highly optimized behavior; re-registering an adapter name overrides the authored Lua binding.

Lua and JSON helpers are intentionally not frame-hot APIs. Use them for startup, scene setup, adapter callbacks, persistence, and reload flows. For repeated per-frame entity access, prefer caching the actor wrapper or using exact names over repeated tag scans.

### Script Hot Reload

Development builds can reload Lua modules without recreating the game session:

```c
sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
sdl3d_asset_resolver_mount_directory(assets, "data", error, sizeof(error));
sdl3d_game_data_reload_scripts(runtime, assets, error, sizeof(error));
sdl3d_asset_resolver_destroy(assets);
```

Reload is atomic. The runtime creates a fresh Lua state, reloads the script
manifest through the supplied resolver, resolves authored Lua adapter functions,
and commits only if every referenced script and adapter function is valid. If a
script has a syntax error, returns a non-table value, is missing, or drops an
adapter function, the existing script state remains active.

Native adapter overrides remain active across reloads. This keeps host
integrations explicit while still allowing authored Lua behavior to iterate
quickly.

Good Pong adapters:

- `adapter.pong.serve_random`: choose a constrained non-extreme ball serve vector.
- `adapter.pong.reflect_from_paddle`: apply Pong-specific deflection based on hit offset.
- `adapter.pong.cpu_track_ball`: move the CPU paddle toward the ball.

Lua scripts should prefer `ctx:actor_with_tags("role", "qualifier")` for role lookups when exact entity names
are not part of the rule. This keeps data free to rename actors while preserving their authored tags. On hot paths,
prefer exact names and cached references because tag-based resolution scans the actor registry.

Bad Pong adapters:

- increment score
- start a timer
- emit a signal
- reset a transform
- toggle a camera

Those are generic actions and should remain data.

## Network Replication

Games that support network play can author a top-level `network` block. The
block is optional; local-only games can omit it entirely and no networking
schema hash is produced.

The network schema supports protocol metadata, host/client replication channels,
typed actor fields, replicated input actions, and control messages:

```json
{
  "network": {
    "protocol": {
      "id": "sdl3d.pong.v1",
      "version": 1,
      "transport": "udp",
      "tick_rate": 60
    },
    "scene_state": {
      "host": {
        "status": "multiplayer_host_status",
        "endpoint": "multiplayer_host_endpoint",
        "peer": "multiplayer_host_client",
        "connected": "multiplayer_host_connected"
      }
    },
    "session_flow": {
      "scenes": {
        "play": "scene.play",
        "host_lobby": "scene.multiplayer.lobby",
        "join": "scene.multiplayer.join",
        "direct_connect": "scene.multiplayer.direct_connect",
        "discovery": "scene.multiplayer.discovery",
        "title": "scene.title"
      },
      "state_keys": {
        "match_mode": "match_mode",
        "network_role": "network_role",
        "network_flow": "network_flow"
      },
      "state_values": {
        "match_mode": { "network": "lan" },
        "network_role": { "host": "host", "client": "client" },
        "network_flow": { "host": "host", "direct": "direct" }
      }
    },
    "runtime_bindings": {
      "replication": {
        "state_snapshot": "play_state",
        "client_input": "client_input"
      },
      "controls": {
        "pause_request": "pause_request",
        "disconnect": "disconnect"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": [
              "position",
              { "path": "properties.velocity", "type": "vec2" },
              { "path": "properties.active_motion", "type": "bool" }
            ]
          }
        ]
      },
      {
        "name": "client_input",
        "direction": "client_to_host",
        "rate": 60,
        "inputs": [
          { "action": "action.paddle.remote.up" },
          { "action": "action.paddle.remote.down" }
        ]
      }
    ],
    "control_messages": [
      { "name": "pause_request", "direction": "client_to_host", "signal": "signal.network.pause" },
      { "name": "disconnect", "direction": "bidirectional", "signal": "signal.network.disconnect" }
    ]
  }
}
```

`protocol.id` must be a non-empty string. `protocol.version` and
`protocol.tick_rate` must be positive integers. `protocol.transport` currently
supports `udp`.

`scene_state` is optional. It is a game-authored map of named network/session
UI state keys, grouped by scope. Host integration code can resolve entries with
`sdl3d_game_data_get_network_scene_state_key()` and write status values into
`sdl3d_game_data_mutable_scene_state()` without hard-coding the property names
used by lobby or connection scenes. All scopes and values must be objects of
non-empty string keys to non-empty string scene-state property names. These keys
are presentation/orchestration metadata and are intentionally not part of the
network schema hash.

`session_flow` is optional. It gives host integration code semantic names for
network scene orchestration without baking a particular demo's scene ids or
scene-state strings into C. `scenes` maps semantic names such as `play`,
`host_lobby`, `join`, `direct_connect`, `discovery`, or `title` to validated
scene ids. `state_keys` maps semantic state names to scene-state property keys.
`state_values` maps semantic values inside each state group to concrete strings
stored in scene state. `messages` maps semantic text groups to non-empty strings
for host-owned network UI flows, such as disconnect reasons or termination
prompts. Prompt strings may use host-specific placeholders such as `{reason}`
when the caller documents and substitutes them. Callers resolve these values with
`sdl3d_game_data_get_network_session_scene()`,
`sdl3d_game_data_get_network_session_state_key()`, and
`sdl3d_game_data_get_network_session_state_value()`, and
`sdl3d_game_data_get_network_session_message()`. Like `scene_state`, these
orchestration maps are not part of the replication schema hash.

`runtime_bindings` is optional. It maps host integration semantics to concrete
authored replication channels and control messages without baking those schema
names into game host code. `replication` maps semantic names to entries in
`network.replication`, and `controls` maps semantic names to entries in
`network.control_messages`. `actions` maps semantic names to input actions, and
`signals` maps semantic names to signals. Values are validated as references.
Callers resolve them with `sdl3d_game_data_get_network_runtime_replication()`,
`sdl3d_game_data_get_network_runtime_control()`,
`sdl3d_game_data_get_network_runtime_action()`, and
`sdl3d_game_data_get_network_runtime_signal()`. `pause` can additionally bind
a network pause action and the bool actor property that mirrors pause state into
replicated game data:

```json
"runtime_bindings": {
  "replication": {
    "state_snapshot": "play_state",
    "client_input": "client_input"
  },
  "controls": {
    "pause_request": "pause_request",
    "resume_request": "resume_request"
  },
  "actions": {
    "menu_back": "action.menu.back",
    "camera_toggle": "action.camera.ball.toggle"
  },
  "signals": {
    "lobby_start": "signal.multiplayer.lobby.start",
    "ui_select": "signal.ui.menu.select"
  },
  "pause": {
    "action": "action.pause",
    "state": {
      "actor": "entity.match",
      "property": "paused"
    }
  }
}
```

The `pause.action` value must reference an input action. `pause.state.actor`
must reference an entity, and `pause.state.property` must name a bool property at
runtime. The referenced property should be declared on the actor with a bool type
and a sane default value, usually `false`, so network pause reads have a defined
state before the first replicated snapshot. Host code can resolve these bindings with
`sdl3d_game_data_get_network_runtime_action()`,
`sdl3d_game_data_get_network_runtime_signal()`,
`sdl3d_game_data_get_network_runtime_pause_action()`,
`sdl3d_game_data_get_network_runtime_pause_state()`, and
`sdl3d_game_data_set_network_runtime_pause_state()`. These maps are runtime
orchestration metadata and are not part of the replication schema hash.

Replication channel directions are `host_to_client` or `client_to_host`, and
`rate` must be a positive integer.
Host-to-client channels author `actors`; each actor must reference an existing
entity and declare a non-empty `fields` array. Field strings are allowed for
built-in transform fields with known types (`position`, `rotation`, `scale`,
all encoded as `vec3`). Property fields should use object form with an explicit
`path` and `type`. Supported field types are `bool`, `int32`, `float32`,
`enum_id`, `vec2`, and `vec3`; aliases `boolean`, `int`, `float`, and
`string_id` are accepted where obvious. The generic JSON type name `number` is
not accepted for network fields; authors should choose `int32` or `float32`
explicitly. Duplicate actor entries and duplicate fields within an actor are
rejected.

Client-to-host channels author `inputs`; each input must reference an existing
input action. Duplicate input actions within a channel are rejected.

Control message directions are `host_to_client`, `client_to_host`, or
`bidirectional`. Each control message must have a unique `name` and reference
an existing `signal`.

Host-to-client actor channels can be encoded into strict snapshot packets with
`sdl3d_game_data_encode_network_snapshot()` and applied with
`sdl3d_game_data_apply_network_snapshot()`. Snapshot packets include the schema
hash, channel index, authoritative tick, field count, field type tags, and field
values in authored order. Decoders reject unsupported packet versions,
mismatched schema hashes, wrong field counts, wrong field tags, truncation, and
trailing bytes. Built-in `position` fields update the actor transform; object
paths under `properties.` update actor properties. `vec2` property replication
is stored in SDL3D's existing vec3 property bag by updating x/y and preserving z.
Callers that need diagnostic logging can use
`sdl3d_game_data_describe_network_snapshot()` to format the active scene,
authored `session_flow` state values, and every replicated actor field in schema
order. This keeps debug output tied to the authored replication channel instead
of hard-coding game actor ids or property paths in host C.

Client-to-host input channels can be encoded with
`sdl3d_game_data_encode_network_input()` and applied with
`sdl3d_game_data_apply_network_input()`. Input packets use the same strict
schema-hash and channel-index checks as snapshots, then store each authored
action value as a tagged `float32` in schema order. Applying a packet writes
the decoded values into the destination `sdl3d_input_manager` as action
overrides, so the next input update exposes the remote actions through the
normal action snapshot APIs. Malformed packets are rejected before any override
is changed. When a peer disconnects or a network scene exits, callers should
use `sdl3d_game_data_clear_network_input_overrides()` for the same channel so
remote input cannot leak into later local play.

Control messages can be encoded with
`sdl3d_game_data_encode_network_control()`, decoded with
`sdl3d_game_data_decode_network_control()`, and emitted through the runtime
signal bus with `sdl3d_game_data_apply_network_control()`. Control packets
carry the schema hash, authored control-message index, and tick; decoders
reject unsupported versions, mismatched schema hashes, invalid control indexes,
truncation, and trailing bytes. Applying a valid control message emits the
authored signal with a payload containing `network_control`,
`network_direction`, and `network_tick`.

When a runtime loads game data with a valid `network` block, it computes a
deterministic schema hash over protocol, replication, input, and control-message
shape. Runtime callers can query it with
`sdl3d_game_data_has_network_schema()` and
`sdl3d_game_data_get_network_schema_hash()`. The hash intentionally ignores
unrelated metadata and presentation data. Network handshakes should use it to
reject clients with incompatible authored schemas before gameplay starts, and
snapshot application enforces the same hash on every packet.

## Validation

Game data is validated before runtime state is instantiated. Host tools can call `sdl3d_game_data_validate_file()` directly to collect diagnostics without creating actors, input bindings, timers, or signal handlers. `sdl3d_game_data_load_file()` runs the same validation pass before loading scripts and wiring logic.

Diagnostics are designed for authored content. Errors include the source file, a best-effort JSON path, and the referenced name when a reference cannot be resolved. For example, a binding action that targets a missing entity reports the action path and the missing entity name. Validation currently checks:

- schema support
- duplicate names within entity, signal, script, adapter, input action, timer, camera, font, and sensor namespaces
- script ids, modules, dependencies, dependency cycles, and script file existence
- input binding structure
- network protocol, replication directions, scene-state key maps, session-flow maps, runtime binding maps, actor/property/action/signal references, supported field types, duplicate network fields, and schema hash shape
- app lifecycle, UI, render effect, sensor, timer, binding, action, component, adapter, light, and camera references
- supported generic logic action types and required action payloads
- warnings for suspicious data such as unused adapters, unused scripts, or unsupported component types

Validation warnings are non-fatal by default. Tooling can set `treat_warnings_as_errors` in `sdl3d_game_data_validation_options` when strict authoring is desired.

## Pong Data Proof

`demos/pong/data/pong.game.json` is the first concrete file using this format. It is loaded by the Pong demo through `sdl3d_game_data_load_file()`, which instantiates the authored actors, input actions, signals, timers, sensors, and bindings into the managed game session.

- fixed-screen non-sector world
- player and CPU paddles
- ball, walls, goals, scores, cameras, lights, particles
- input actions
- named signals
- bounds/contact sensors
- score/reset/serve/win bindings
- adapter calls only for serve randomness, paddle reflection, and CPU steering

Pong keeps specialized math and policy in Lua adapters loaded from `demos/pong/data/scripts/pong.lua`:

- `adapter.pong.serve_random`
- `adapter.pong.reflect_from_paddle`
- `adapter.pong.cpu_track_ball`

The JSON decides when those adapters run and handles the ordinary composition around them, such as score increments, round reset, serve timers, camera toggles, and goal/wall/contact sensors. The Pong executable does not register Pong-specific C rule callbacks; its C code owns presentation, rendering, and process lifecycle.

## Loader Acceptance Criteria

The runtime loader should:

- parse `schema` and reject unsupported versions
- reject duplicate entity, signal, timer, sensor, and binding names
- validate references before gameplay starts
- validate script ids, modules, dependency order, and adapter function references before gameplay starts
- validate component/action payload types
- preserve unknown properties under entity property bags
- allow unknown component types only when configured for permissive tooling mode
- expose precise error paths
- instantiate actor registry entries, signal names, timer definitions, and logic bindings without requiring sectors
