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


def parse_layer_bindings(layer: str, source: Path) -> list[str]:
    match = re.search(r"bindings\s*=\s*<(?P<body>.*?)>;", layer, re.DOTALL)
    if match is None:
        fail(f"{source}: layer bindings are missing")
    result: list[str] = []
    for line in match.group("body").splitlines():
        starts = list(re.finditer(r"&[A-Za-z0-9_]+", line))
        for index, start in enumerate(starts):
            end = starts[index + 1].start() if index + 1 < len(starts) else len(line)
            result.append(re.sub(r"\s+", " ", line[start.start():end].strip()))
    return result


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
        "<&pmw_gesture_2_processor>,",
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
    normalized_right_pmw = re.sub(r"\s+", "", right_pmw_text)
    require(
        normalized_right_pmw,
        "<&pmw_gesture_2_processor>,<&pmw_gesture_processor>,<&zip_temp_layer110000>;",
        right_pmw_listener,
    )

    right_overlay = repo / "boards/shields/zen/zen_right.overlay"
    right_text = read_text(right_overlay)
    for expected in (
        "require-prior-idle-ms = <300>;",
        "<&zip_temp_layer 1 10000>;",
    ):
        require(right_text, expected, right_overlay)
    normalized_right = re.sub(r"\s+", "", right_text)
    require(
        normalized_right,
        "<&gesture_2_processor>,<&gesture_processor>,<&zip_temp_layer110000>;",
        right_overlay,
    )

    listener = repo / "snippets/input-listener/input-listener.overlay"
    listener_text = read_text(listener)
    for expected in (
        "&mkp_input_listener {",
        "input-processors = <&zip_temp_layer 1 10000>;",
    ):
        require(listener_text, expected, listener)

    keymap = repo / "config/keymap.keymap"
    keymap_text = read_text(keymap)
    mouse_lt = re.search(
        r"^\s*mouse_lt:\s*mouse_layer_tap\s*\{(?P<body>.*?)^\s*\};",
        keymap_text,
        re.MULTILINE | re.DOTALL,
    )
    if mouse_lt is None:
        fail(f"{keymap}: Mouse Layer-Tap behavior is missing")
    mouse_lt_body = " ".join(mouse_lt.group("body").split())
    for expected in (
        'compatible = "zmk,behavior-hold-tap";',
        "#binding-cells = <2>;",
        'flavor = "tap-preferred";',
        "tapping-term-ms = <300>;",
        "bindings = <&mo>, <&mkp>;",
        'display-name = "Mouse Layer-Tap";',
    ):
        require(mouse_lt_body, expected, keymap)
    layer_ids = [
        int(value)
        for value in re.findall(r"^\s*layer_(\d+)\s*\{", keymap_text, re.MULTILINE)
    ]
    if layer_ids != list(range(10)):
        fail(f"{keymap}: expected layers 0 through 9, found {layer_ids}")
    expected_layer_names = {
        0: "Base",
        1: "Mouse",
        2: "Scroll",
        3: "Gesture 1",
        4: "Gesture 2",
        5: "symbol",
        6: "number",
        7: "move",
        8: "setting",
        9: "User 9",
    }
    layers = {}
    for layer_id, display_name in expected_layer_names.items():
        layer = re.search(
            rf"^\s*layer_{layer_id}\s*\{{(?P<body>.*?)^\s*\}};",
            keymap_text,
            re.MULTILINE | re.DOTALL,
        )
        if layer is None:
            fail(f"{keymap}: missing layer {layer_id}")
        layers[layer_id] = layer.group("body")
        require(layers[layer_id], f'display-name = "{display_name}";', keymap)

    bindings_by_layer = {
        layer_id: parse_layer_bindings(layer, keymap) for layer_id, layer in layers.items()
    }
    for layer_id, bindings in bindings_by_layer.items():
        if len(bindings) != 50:
            fail(f"{keymap}: layer {layer_id} must retain all 50 editable slots")

    mouse_bindings = re.search(
        r"bindings\s*=\s*<(?P<body>.*?)>;", layers[1], re.DOTALL
    )
    if mouse_bindings is None:
        fail(f"{keymap}: Mouse layer bindings are missing")
    mouse_binding_text = mouse_bindings.group("body")
    for mouse_button in ("MB1", "MB2", "MB3"):
        if re.search(
            rf"&(?:mkp\s+{mouse_button}|mouse_lt\s+\d+\s+{mouse_button})\b",
            mouse_binding_text,
        ) is None:
            fail(f"{keymap}: Mouse layer is missing {mouse_button}")
    mouse_behaviors = re.findall(r"&([A-Za-z0-9_]+)\b", mouse_binding_text)
    configured_mouse_positions = [
        position
        for position, behavior in enumerate(mouse_behaviors)
        if behavior not in {"trans", "none"}
    ]
    excluded_match = re.search(
        r"excluded-positions\s*=\s*<(?P<body>[^>]*)>;", right_text
    )
    if excluded_match is None:
        fail(f"{right_overlay}: excluded-positions is missing")
    excluded_positions = [int(value) for value in excluded_match.group("body").split()]
    if excluded_positions != configured_mouse_positions:
        fail(
            f"{right_overlay}: excluded positions {excluded_positions} do not match "
            f"configured Mouse layer positions {configured_mouse_positions}"
        )

    if bindings_by_layer[1][22] != "&kp RIGHT_COMMAND":
        fail(f"{keymap}: Mouse position 22 must retain the requested right Command key")
    gesture_2_access = bindings_by_layer[0] + bindings_by_layer[1]
    if not any(re.match(r"&(?:lt|mo)\s+4\b", item) for item in gesture_2_access):
        fail(f"{keymap}: Gesture 2 must remain reachable from Base or Mouse")
    for layer_id in (3, 4):
        for position in (8, 19, 21, 34):
            behavior = bindings_by_layer[layer_id][position].split()[0]
            if behavior in {"&trans", "&none"}:
                fail(f"{keymap}: Gesture layer {layer_id} action slot {position} is empty")
    base_bindings = " ".join(bindings_by_layer[0])
    for expected in ("&lt 5 LANG1", "&lt 6 SPACE", "&lt 7 ENTER"):
        require(base_bindings, expected, keymap)
    if not any(item == "&mo 8" for item in bindings_by_layer[5]):
        fail(f"{keymap}: Symbol-to-setting binding did not move with setting")

    base_dtsi = repo / "boards/shields/zen/zen.dtsi"
    base_text = read_text(base_dtsi)
    for expected in (
        "gesture_processor: gesture_processor {",
        "binding-layer = <3>;",
        "gesture_2_processor: gesture_2_processor {",
        "binding-layer = <4>;",
        "threshold = <30>;",
        "reset-on-layer = <2>;",
    ):
        require(base_text, expected, base_dtsi)

    left_listeners = repo / "snippets/input-split-listener-left-all/input-split-listener-left-all.overlay"
    left_text = read_text(left_listeners)
    for expected in (
        "pmw_gesture_processor: pmw_gesture_processor {",
        "pmw_gesture_2_processor: pmw_gesture_2_processor {",
        "binding-layer = <4>;",
        "threshold = <40>;",
        "<&left_pmw3610_scroll_scaler 3 80>;",
    ):
        require(left_text, expected, left_listeners)
    normalized_left = re.sub(r"\s+", "", left_text)
    require(
        normalized_left,
        "<&pmw_gesture_2_processor>,<&pmw_gesture_processor>,<&zip_temp_layer1500>;",
        left_listeners,
    )
    if normalized_left.count(
        "<&gesture_2_processor>,<&gesture_processor>,<&zip_temp_layer1500>;"
    ) != 2:
        fail(f"{left_listeners}: PAW3222 and trackpad Gesture 2 chains are incomplete")

    split_listener = repo / "snippets/input-split-listener/input-split-listener.overlay"
    split_text = re.sub(r"\s+", "", read_text(split_listener))
    require(
        split_text,
        "<&zip_xy_scaler156>,<&gesture_2_processor>,<&gesture_processor>;",
        split_listener,
    )

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
        "excluded-positions = < 0x13 0x14 0x15 0x16 0x18 0x26 0x27 0x29 >;",
        "< &pmw3610_scroll_scaler 0x1 0x28 >;",
        "< &pmw_gesture_2_processor >, < &pmw_gesture_processor >, < &zip_temp_layer 0x1 0x2710 >;",
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
