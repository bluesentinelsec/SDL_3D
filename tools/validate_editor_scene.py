#!/usr/bin/env python3
"""Validate repeated-widget invariants in the Slayer3D editor scene data.

The editor shell scene enumerates many repeated widgets by hand (inspector
property rows, scroll thumbs, panel chrome). Manual duplication has caused UI
drift in the past, so this script checks the invariants that drifted before:

- inspector property row slots are contiguous from 0 to the slot cap
- every property row repeats the same geometry, actions, and slot bindings
- scroll thumbs stay inside their scroll tracks
- authored editor data does not reference retired surfaces
  (the editor_shell_dojo directory, the legacy palette modal, or the
  removed editor.file.edit.focus state)

Usage:
    python3 tools/validate_editor_scene.py [--data-dir apps/slayer3d_editor/data]

Exits non-zero and prints one line per violation when the data drifts.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

PROPERTY_SLOT_CAP = 64  # keep in sync with SLAYER3D_EDITOR_PROPERTY_SLOT_CAP
PROPERTY_ROW_PREFIX = "ui.editor_shell.left_inspector.row.property"
SCENE_NAME = "scene.slayer3d_editor.main"
RETIRED_PATTERNS = (
    ("dojo", "retired editor_shell_dojo naming"),
    ('"ui.editor_shell.palette.', "retired legacy palette modal widget"),
    ("editor.file.edit.focus", "removed editor.file.edit.focus state"),
    ("editor.file.edit.replace_on_text", "removed editor.file.edit state"),
    ('"ui.editor_shell.view_menu.', "retired View menu widget"),
)

errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def iter_widgets(node, parent=None):
    """Yield (widget, parent_widget) for every dict carrying an "id"."""
    if isinstance(node, dict):
        this = node if "id" in node else parent
        if "id" in node:
            yield node, parent
        for value in node.values():
            yield from iter_widgets(value, this)
    elif isinstance(node, list):
        for value in node:
            yield from iter_widgets(value, parent)


def numeric_rect(widget):
    rect = tuple(widget.get(key) for key in ("x", "y", "w", "h"))
    if all(isinstance(value, (int, float)) for value in rect):
        return rect
    return None


def check_retired_references(path: Path) -> None:
    text = path.read_text()
    for needle, description in RETIRED_PATTERNS:
        if needle in text:
            line = next(
                i + 1 for i, l in enumerate(text.splitlines()) if needle in l
            )
            fail(f"{path.name}:{line}: references {description} ({needle!r})")


def check_property_rows(widgets_by_id) -> None:
    row_pattern = re.compile(re.escape(PROPERTY_ROW_PREFIX) + r"(\d+)$")
    slots = sorted(
        int(match.group(1))
        for widget_id in widgets_by_id
        if (match := row_pattern.match(widget_id))
    )
    expected = list(range(PROPERTY_SLOT_CAP))
    if slots != expected:
        fail(
            f"property rows are not contiguous 0..{PROPERTY_SLOT_CAP - 1}: "
            f"found {len(slots)} rows, min {slots[0] if slots else 'none'}, "
            f"max {slots[-1] if slots else 'none'}"
        )
        return

    reference = widgets_by_id[f"{PROPERTY_ROW_PREFIX}0"]
    for slot in slots:
        row = widgets_by_id[f"{PROPERTY_ROW_PREFIX}{slot}"]
        for key in ("x", "w", "h", "layer", "type", "layout", "scroll_y_key", "clip_rect_id"):
            if row.get(key) != reference.get(key):
                fail(f"row.property{slot}: field {key!r} diverges from row.property0")
        if row.get("action") != f"editor.property.select_slot.{slot}":
            fail(f"row.property{slot}: action is {row.get('action')!r}")
        conditions = json.dumps(row.get("visible_if", {}))
        if f"editor.property.slot.{slot}.available" not in conditions:
            fail(f"row.property{slot}: visible_if does not gate on slot {slot} availability")

        children = {child.get("id", ""): child for child in row.get("children", [])}
        for suffix, action_prefix, binding_key in (
            ("label", "editor.property.select_slot_key.", f"editor.property.slot.{slot}.key"),
            ("value", "editor.property.select_slot_value.", f"editor.property.slot.{slot}.value"),
            ("delete", "editor.property.delete.", None),
        ):
            child = children.get(f"{PROPERTY_ROW_PREFIX}{slot}.{suffix}")
            if child is None:
                fail(f"row.property{slot}: missing child {suffix!r}")
                continue
            if child.get("action") != f"{action_prefix}{slot}":
                fail(f"row.property{slot}.{suffix}: action is {child.get('action')!r}")
            if binding_key is not None:
                bindings = json.dumps(child.get("bindings", []))
                if binding_key not in bindings:
                    fail(f"row.property{slot}.{suffix}: does not bind {binding_key}")


def check_scroll_thumbs(widget_parents) -> None:
    widgets_by_id = {widget["id"]: widget for widget, _ in widget_parents}
    parents = {widget["id"]: parent for widget, parent in widget_parents}
    thumb_pattern = re.compile(r"^(?P<base>.*\.scroll)\.thumb(\.|$)")
    for widget_id, widget in widgets_by_id.items():
        match = thumb_pattern.match(widget_id)
        if match is None:
            continue
        thumb_rect = numeric_rect(widget)
        if thumb_rect is None:
            continue
        track = widgets_by_id.get(f"{match.group('base')}.track")
        tx, ty, tw, th = (0, 0, 0, 0)
        container = None
        if track is not None and numeric_rect(track) is not None:
            if parents.get(widget_id) is parents.get(track["id"]):
                container = track["id"]
                tx, ty, tw, th = numeric_rect(track)
        if container is None:
            parent = parents.get(widget_id)
            if parent is None or numeric_rect(parent) is None:
                continue
            container = parent["id"]
            # children are positioned relative to their parent panel
            tx, ty = 0, 0
            _, _, tw, th = numeric_rect(parent)
        x, y, w, h = thumb_rect
        if x < tx or y < ty or x + w > tx + tw or y + h > ty + th:
            fail(
                f"{widget_id}: rect ({x},{y},{w},{h}) escapes container "
                f"{container} ({tx},{ty},{tw},{th})"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path("apps/slayer3d_editor/data"),
        help="editor data directory to validate",
    )
    args = parser.parse_args()

    scene_path = args.data_dir / "scenes" / "editor_shell.scene.json"
    game_path = args.data_dir / "slayer3d_editor.game.json"
    for path in (scene_path, game_path):
        if not path.exists():
            print(f"error: missing data file {path}", file=sys.stderr)
            return 2

    scene = json.loads(scene_path.read_text())
    if scene.get("name") != SCENE_NAME:
        fail(f"scene name is {scene.get('name')!r}, expected {SCENE_NAME!r}")

    check_retired_references(scene_path)
    check_retired_references(game_path)

    widget_parents = list(iter_widgets(scene))
    widgets_by_id = {widget["id"]: widget for widget, _ in widget_parents}
    check_property_rows(widgets_by_id)
    check_scroll_thumbs(widget_parents)

    if errors:
        for message in errors:
            print(f"FAIL: {message}", file=sys.stderr)
        print(f"{len(errors)} editor scene validation error(s)", file=sys.stderr)
        return 1
    print("editor scene validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
