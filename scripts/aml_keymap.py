"""Parse the plain Devicetree bindings saved by Keymap Editor."""

from __future__ import annotations

import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def layer_body(keymap: str, node_name: str) -> str:
    match = re.search(
        rf"^\s*{re.escape(node_name)}\s*\{{(?P<body>.*?)^\s*\}};",
        strip_comments(keymap), re.MULTILINE | re.DOTALL,
    )
    require(match is not None, f"missing keymap node {node_name}")
    return match.group("body")


def bindings(layer: str) -> list[str]:
    match = re.search(r"(?<!sensor-)\bbindings\s*=\s*<(?P<body>.*?)>;",
                      strip_comments(layer), re.DOTALL)
    require(match is not None, "layer bindings are missing")
    body = match.group("body")
    starts = list(re.finditer(r"&[A-Za-z0-9_]+", body))
    result = []
    for index, start in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(body)
        result.append(re.sub(r"\s+", " ", body[start.start():end].strip()))
    return result


def mouse_positions(keymap: str, mouse_layer: int = 1, key_count: int = 42,
                    layer_node: str | None = None) -> list[int]:
    require(mouse_layer >= 0 and key_count > 0, "invalid Mouse layer or key count")
    items = bindings(layer_body(keymap, layer_node or f"layer_{mouse_layer}"))
    require(len(items) == key_count, f"Mouse layer must retain all {key_count} editable slots: {len(items)}")
    return [position for position, item in enumerate(items)
            if item.split()[0] not in {"&trans", "&none"}]


def aml_header(keymap: str, mouse_layer: int = 1, key_count: int = 42,
               layer_node: str | None = None) -> str:
    # The stock temp-layer driver skips cancellation when the array is empty.
    # An impossible position keeps its handler enabled for an all-transparent layer.
    positions = mouse_positions(keymap, mouse_layer, key_count, layer_node) or [65535]
    return ("/* Generated from the selected keymap; do not edit. */\n"
            "#define AML_EXCLUDED_POSITIONS "
            + " ".join(map(str, positions)) + "\n")


def keymap_source(path: Path, seen: set[Path] | None = None) -> tuple[str, list[Path]]:
    """Resolve wrapper-only quoted includes used by shield default keymaps."""
    path = path.resolve()
    seen = set() if seen is None else seen
    require(path not in seen, f"cyclic keymap wrapper include: {path}")
    seen.add(path)
    text = path.read_text(encoding="utf-8")
    wrapper = re.fullmatch(r'\s*#include\s+"([^"\n]+)"\s*', strip_comments(text))
    if wrapper:
        source, inputs = keymap_source(path.parent / wrapper.group(1), seen)
        return source, [path, *inputs]
    return text, [path]
