#!/usr/bin/env python3
"""Verify the static ZEN firmware configuration and built artifact."""

from __future__ import annotations

import argparse
import re
import struct
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


def pointer_acceleration_settings(repo: Path) -> tuple[dict[str, int], bool]:
    overlay = (
        repo
        / "snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.overlay"
    )
    text = read_text(overlay)

    def value(property_name: str) -> int:
        match = re.search(rf"{property_name}\s*=\s*<(\d+)>;", text)
        if match is None:
            fail(f"{overlay}: missing numeric {property_name}")
        return int(match.group(1))

    settings = {
        name: value(name)
        for name in (
            "zen-pointer-acceleration-base-gain-milli",
            "zen-pointer-acceleration-takeoff-speed",
            "zen-pointer-acceleration-full-speed",
            "zen-pointer-acceleration-max-gain-milli",
            "zen-pointer-acceleration-reference-interval-ms",
            "zen-pointer-acceleration-idle-reset-ms",
            "zen-pointer-acceleration-scroll-layer",
            "zen-pointer-acceleration-gesture-layer",
        )
    }
    takeoff = settings["zen-pointer-acceleration-takeoff-speed"]
    full = settings["zen-pointer-acceleration-full-speed"]
    base = settings["zen-pointer-acceleration-base-gain-milli"]
    maximum = settings["zen-pointer-acceleration-max-gain-milli"]
    reference = settings["zen-pointer-acceleration-reference-interval-ms"]
    idle = settings["zen-pointer-acceleration-idle-reset-ms"]
    if not 0 <= takeoff < full <= 65535:
        fail(f"{overlay}: expected 0 <= takeoff-speed < full-speed <= 65535")
    if not 500 <= base <= 1000:
        fail(f"{overlay}: base-gain-milli must be in [500, 1000]")
    if not base <= maximum <= 4000:
        fail(f"{overlay}: max-gain-milli must be between base and 4000")
    if not 0 < reference < idle <= 65535:
        fail(f"{overlay}: expected 0 < reference-interval-ms < idle-reset-ms <= 65535")
    if settings["zen-pointer-acceleration-scroll-layer"] != 2:
        fail(f"{overlay}: Scroll bypass must remain on layer 2")
    if settings["zen-pointer-acceleration-gesture-layer"] != 3:
        fail(f"{overlay}: Gesture bypass must remain on layer 3")
    require(text, "zen-pointer-acceleration;", overlay)

    conf = (
        repo
        / "snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.conf"
    )
    conf_text = read_text(conf)
    enabled_match = re.search(r"^CONFIG_ZEN_POINTER_ACCELERATION=([yn])$", conf_text, re.MULTILINE)
    if enabled_match is None:
        fail(f"{conf}: expected CONFIG_ZEN_POINTER_ACCELERATION=y or n")

    return settings, enabled_match.group(1) == "y"


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
        "cpi = <1200>;",
        "force-awake;",
    ):
        require(pmw_text, expected, pmw_overlay)

    right_pmw_listener = repo / "snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.overlay"
    right_pmw_text = read_text(right_pmw_listener)
    for expected in (
        "pmw3610_scroll_scaler: pmw3610_scroll_scaler",
        "&pointing_device {",
        "zen-pointer-acceleration;",
        "<&pmw_gesture_processor>,\n        <&zip_temp_layer 1 10000>;",
        "<&pmw3610_scroll_scaler 1 60>;",
    ):
        require(right_pmw_text, expected, right_pmw_listener)
    pointer_acceleration_settings(repo)
    for unexpected in (
        "pointer_acceleration_output_listener",
        "device = <&pointer_acceleration>;",
        'compatible = "zmk,input-processor-pointer-acceleration";',
        "<&pointer_acceleration>",
    ):
        reject(right_pmw_text, unexpected, right_pmw_listener)
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
        "threshold = <200>;",
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


def verify_build(build_dir: Path, repo: Path) -> None:
    config = build_dir / "zephyr/.config"
    config_text = read_text(config)
    for expected in (
        "CONFIG_PMW3610_ALT=y",
        "CONFIG_ZMK_STUDIO=y",
        "CONFIG_ZMK_STUDIO_RPC=y",
    ):
        require(config_text, expected, config)

    acceleration, enabled = pointer_acceleration_settings(repo)
    if enabled:
        require(config_text, "CONFIG_ZEN_POINTER_ACCELERATION=y", config)
    else:
        require(config_text, "# CONFIG_ZEN_POINTER_ACCELERATION is not set", config)
    for unexpected in (
        "CONFIG_ZMK_SETTINGS_RPC=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_INPUT_PROCESSOR_POINTER_ACCELERATION=y",
    ):
        reject(config_text, unexpected, config)

    dts = build_dir / "zephyr/zephyr.dts"
    dts_text = " ".join(read_text(dts).split())
    for expected in (
        'compatible = "pixart,pmw3610-alt";',
        "cpi = < 0x4b0 >;",
        "require-prior-idle-ms = < 0x12c >;",
        "excluded-positions = < 0x13 0x14 0x15 0x18 0x26 0x27 0x29 >;",
        "< &pmw3610_scroll_scaler 0x1 0x3c >;",
        *(f"{name} = < 0x{value:x} >;" for name, value in acceleration.items()),
        "zen-pointer-acceleration;",
        "< &pmw_gesture_processor >, < &zip_temp_layer 0x1 0x2710 >;",
        "< &zip_xy_scaler 0x1 0x38 >, < &zip_xy_transform 0x3 >, < &zip_xy_to_scroll_mapper >, < &left_pmw3610_scroll_scaler 0x3 0x50 >;",
    ):
        require(dts_text, expected, dts)
    for unexpected in (
        "runtime_input_processor",
        'compatible = "cormoran,pmw3610";',
        'device = < &pointer_acceleration >;',
        'compatible = "zmk,input-processor-pointer-acceleration";',
        "< &pointer_acceleration >",
    ):
        reject(dts_text, unexpected, dts)


def uf2_payload(data: bytes) -> bytes:
    if len(data) == 0 or len(data) % 512 != 0:
        return data

    blocks: list[tuple[int, bytes]] = []
    for offset in range(0, len(data), 512):
        block = data[offset : offset + 512]
        magic0, magic1, _flags, address, size = struct.unpack_from("<IIIII", block)
        (magic_end,) = struct.unpack_from("<I", block, 508)
        if magic0 != 0x0A324655 or magic1 != 0x9E5D5157 or magic_end != 0x0AB16F30:
            return data
        if size > 476:
            fail("invalid UF2 payload size")
        blocks.append((address, block[32 : 32 + size]))

    blocks.sort(key=lambda item: item[0])
    payload = bytearray()
    next_address = blocks[0][0]
    for address, block_payload in blocks:
        if address < next_address:
            fail("overlapping UF2 payload blocks")
        payload.extend(b"\xff" * (address - next_address))
        payload.extend(block_payload)
        next_address = address + len(block_payload)
    return bytes(payload)


def verify_binary(path: Path) -> None:
    if not path.is_file():
        fail(f"missing firmware/ELF: {path}")
    data = uf2_payload(path.read_bytes()) if path.suffix.lower() == ".uf2" else path.read_bytes()
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
        verify_build(args.build_dir.resolve(), args.repo.resolve())
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
