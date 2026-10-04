#!/usr/bin/env python3
"""Generate AML exclusions without modifying the user's keymap or source tree."""

import argparse
from pathlib import Path

from aml_keymap import aml_header, keymap_source


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("keymap", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mouse-layer", type=int, default=1)
    parser.add_argument("--key-count", type=int, required=True)
    parser.add_argument("--layer-node")
    parser.add_argument("--depfile", type=Path, help="write resolved keymap inputs, one per line")
    args = parser.parse_args()
    source, inputs = keymap_source(args.keymap)
    header = aml_header(source, args.mouse_layer, args.key_count, args.layer_node)
    if args.depfile:
        args.depfile.write_text("\n".join(map(str, inputs)) + "\n", encoding="utf-8")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != header:
        args.output.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
