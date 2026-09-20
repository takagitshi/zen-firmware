#!/usr/bin/env python3
"""Verify the static ZEN firmware configuration and built artifact."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


FORBIDDEN_BINARY_MARKERS = (
    b"cormoran_rip",
    b"cormoran_custom_settings",
    b"cormoran__pmw3610",
)
REQUIRED_BINARY_MARKERS = (
    b"zmk_studio_rpc_notification",
    b"zmk_studio_core_lock_state_changed",
    b"keymap/layer_order",
)


def fail(message: str) -> None:
    raise AssertionError(message)


def read_text(path: Path) -> str:
    if not path.is_file():
        fail(f"missing file: {path}")
    return path.read_text(encoding="utf-8")


def require(text: str, expected: str, source: Path) -> None:
    if expected not in text:
        fail(f"{source}: missing {expected!r}")


def reject(text: str, unexpected: str, source: Path) -> None:
    if unexpected in text:
        fail(f"{source}: unexpected {unexpected!r}")


def verify_sources(repo: Path) -> None:
    west = repo / "config/west.yml"
    west_text = read_text(west)
    if re.search(
        r"- name: zmk\s+remote: zmkfirmware\s+revision: v0\.3\s+import: app/west\.yml",
        west_text,
    ) is None:
        fail(f"{west}: ZMK must use the official zmkfirmware v0.3 project")
    for unexpected in (
        "cormoran",
        "zmk-feature-custom-settings",
        "zmk-module-settings-rpc",
        "zmk-module-runtime-input-processor",
        "zmk-driver-pmw3610-with-custom-studio-rpc",
    ):
        reject(west_text, unexpected, west)

    build_yaml = repo / "build.yaml"
    build_text = read_text(build_yaml)
    require(
        build_text,
        'snippet: "studio-rpc-usb-uart split-central input-trackball-pmw3610 input-listener-right-pmw3610 input-listener input-split-listener-left-all"',
        build_yaml,
    )
    reject(build_text.lower(), "dya", build_yaml)

    for relative in (
        "snippets/dya-runtime-input-settings",
        "snippets/dya-pmw3610-settings",
        "src/dya_pmw3610_settings.c",
    ):
        if (repo / relative).exists():
            fail(f"obsolete DYA path remains: {repo / relative}")

    for relative in (
        "boards/shields/zen/zen_right.conf",
        "boards/shields/zen/zen_left.conf",
    ):
        conf = repo / relative
        conf_text = read_text(conf)
        require(conf_text, "CONFIG_ZMK_STUDIO=y", conf)
        require(conf_text, "CONFIG_ZMK_STUDIO_LOCKING=n", conf)
        for unexpected in (
            "CONFIG_ZMK_SETTINGS_RPC",
            "CONFIG_ZMK_CUSTOM_SETTINGS",
            "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR",
        ):
            reject(conf_text, unexpected, conf)

    pmw_overlay = repo / "snippets/input-trackball-pmw3610/input-trackball-pmw3610.overlay"
    pmw_text = read_text(pmw_overlay)
    for expected in (
        'compatible = "pixart,pmw3610-alt";',
        "cpi = <800>;",
        "force-awake;",
    ):
        require(pmw_text, expected, pmw_overlay)

    right_pmw_listener = repo / "snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.overlay"
    right_pmw_text = read_text(right_pmw_listener)
    for expected in (
        "pmw3610_scroll_scaler: pmw3610_scroll_scaler",
        "<&pmw_gesture_processor>,",
        "<&zip_temp_layer 1 10000>;",
        "<&pmw3610_scroll_scaler 1 40>;",
    ):
        require(right_pmw_text, expected, right_pmw_listener)
    for unexpected in (
        "runtime_input_processor",
        "settings-id",
        'compatible = "cormoran,pmw3610";',
    ):
        reject(pmw_text + right_pmw_text, unexpected, right_pmw_listener)

    right_overlay = repo / "boards/shields/zen/zen_right.overlay"
    right_text = read_text(right_overlay)
    for expected in (
        "require-prior-idle-ms = <300>;",
        "excluded-positions = <19 20 21 22 24 38>;",
        "<&zip_temp_layer 1 10000>;",
    ):
        require(right_text, expected, right_overlay)

    listener = repo / "snippets/input-listener/input-listener.overlay"
    listener_text = read_text(listener)
    for expected in (
        "&mkp_input_listener {",
        "input-processors = <&zip_temp_layer 1 10000>;",
    ):
        require(listener_text, expected, listener)

    keymap = repo / "config/keymap.keymap"
    keymap_text = read_text(keymap)
    for mouse_button in ("MB1", "MB2", "MB3"):
        require(keymap_text, f"&mkp {mouse_button}", keymap)
    layer_ids = [
        int(value)
        for value in re.findall(r"^\s*layer_(\d+)\s*\{", keymap_text, re.MULTILINE)
    ]
    if layer_ids != list(range(9)):
        fail(f"{keymap}: expected layers 0 through 8, found {layer_ids}")
    expected_layer_names = {
        0: "Base",
        1: "Mouse",
        2: "Scroll",
        3: "Gesture",
        4: "symbol",
        5: "number",
        6: "move",
        7: "setting",
        8: "User 8",
    }
    for layer_id, display_name in expected_layer_names.items():
        layer = re.search(
            rf"^\s*layer_{layer_id}\s*\{{(?P<body>.*?)^\s*\}};",
            keymap_text,
            re.MULTILINE | re.DOTALL,
        )
        if layer is None:
            fail(f"{keymap}: missing layer {layer_id}")
        require(layer.group("body"), f'display-name = "{display_name}";', keymap)

    base_dtsi = repo / "boards/shields/zen/zen.dtsi"
    base_text = read_text(base_dtsi)
    for expected in (
        "gesture_processor: gesture_processor {",
        "binding-layer = <3>;",
        "threshold = <30>;",
        "reset-on-layer = <2>;",
    ):
        require(base_text, expected, base_dtsi)

    left_listeners = repo / "snippets/input-split-listener-left-all/input-split-listener-left-all.overlay"
    left_text = read_text(left_listeners)
    for expected in (
        "pmw_gesture_processor: pmw_gesture_processor {",
        "threshold = <40>;",
        "<&left_pmw3610_scroll_scaler 3 80>;",
    ):
        require(left_text, expected, left_listeners)

    source_suffixes = {".c", ".conf", ".dtsi", ".h", ".keymap", ".overlay", ".py", ".yml", ".yaml"}
    repo_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in repo.rglob("*")
        if path.is_file()
        and (
            path.suffix in source_suffixes
            or path.name == "CMakeLists.txt"
            or path.name.startswith("Kconfig")
        )
        and ".git" not in path.parts
        and path.name not in {"verify-zen-build.py", "AGENTS.md"}
        and "docs" not in path.parts
    )
    for unexpected in ("DYA", "dya", "cormoran"):
        reject(repo_text, unexpected, repo)


def verify_build(build_dir: Path) -> None:
    config = build_dir / "zephyr/.config"
    config_text = read_text(config)
    for expected in (
        "CONFIG_PMW3610_ALT=y",
        "CONFIG_ZMK_STUDIO=y",
        "CONFIG_ZMK_STUDIO_RPC=y",
    ):
        require(config_text, expected, config)
    for unexpected in (
        "CONFIG_ZMK_SETTINGS_RPC=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y",
    ):
        reject(config_text, unexpected, config)

    dts = build_dir / "zephyr/zephyr.dts"
    dts_text = " ".join(read_text(dts).split())
    for expected in (
        'compatible = "pixart,pmw3610-alt";',
        "cpi = < 0x320 >;",
        "require-prior-idle-ms = < 0x12c >;",
        "excluded-positions = < 0x13 0x14 0x15 0x16 0x18 0x26 >;",
        "< &pmw3610_scroll_scaler 0x1 0x28 >;",
        "< &pmw_gesture_processor >, < &zip_temp_layer 0x1 0x2710 >;",
        "< &zip_xy_scaler 0x1 0x38 >, < &zip_xy_transform 0x3 >, < &zip_xy_to_scroll_mapper >, < &left_pmw3610_scroll_scaler 0x3 0x50 >;",
    ):
        require(dts_text, expected, dts)
    for unexpected in (
        "runtime_input_processor",
        'compatible = "cormoran,pmw3610";',
    ):
        reject(dts_text, unexpected, dts)


def verify_binary(path: Path) -> None:
    if not path.is_file():
        fail(f"missing firmware/ELF: {path}")
    data = path.read_bytes()
    for marker in REQUIRED_BINARY_MARKERS:
        if marker not in data:
            fail(f"{path}: required ZMK Studio subsystem is missing: {marker.decode()}")
    for marker in FORBIDDEN_BINARY_MARKERS:
        if marker in data:
            fail(f"{path}: removed subsystem remains: {marker.decode()}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()

    verify_sources(args.repo.resolve())
    if args.build_dir:
        verify_build(args.build_dir.resolve())
    if args.firmware:
        verify_binary(args.firmware.resolve())
    print("ZEN build contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"ZEN build contract: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
