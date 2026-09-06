#!/usr/bin/env python3
"""Verify the ZEN DYA Studio source and built-firmware contract."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REQUIRED_SUBSYSTEMS = (
    b"zmk__settings",
    b"cormoran_rip",
    b"cormoran_custom_settings",
)
FORBIDDEN_SUBSYSTEMS = (
    b"cormoran__pmw3610",
    b"zen__pmw3610_settings",
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


def require_pin(text: str, name: str, revision: str, source: Path) -> None:
    pattern = rf"- name: {re.escape(name)}\s+remote: cormoran\s+revision: {revision}"
    if re.search(pattern, text) is None:
        fail(f"{source}: {name} is not pinned to {revision}")


def verify_sources(repo: Path) -> None:
    west = repo / "config/west.yml"
    west_text = read_text(west)
    for name, revision in (
        ("zmk-feature-custom-settings", "c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f"),
        ("zmk-module-settings-rpc", "78f86df9e6c5edaf57bef3ccbd7f360cfdf49291"),
        ("zmk-module-runtime-input-processor", "43618985f8c9d5457cc333b7ca0733f2d361911e"),
    ):
        require_pin(west_text, name, revision, west)
    reject(west_text, "zmk-driver-pmw3610-with-custom-studio-rpc", west)

    cmake = repo / "CMakeLists.txt"
    cmake_text = read_text(cmake)
    require(
        cmake_text,
        "zephyr_library_sources_ifdef(CONFIG_PMW3610_ALT drivers/pmw3610_alt/src/pmw3610.c)",
        cmake,
    )
    reject(cmake_text, "dya_pmw3610_settings.c", cmake)

    kconfig = repo / "Kconfig"
    kconfig_text = read_text(kconfig)
    for expected in (
        "menuconfig PMW3610_ALT",
        "depends on DT_HAS_PIXART_PMW3610_ALT_ENABLED",
        "config PMW3610_ALT_INIT_RETRY_MAX_ATTEMPTS",
    ):
        require(kconfig_text, expected, kconfig)
    reject(kconfig_text, "ZEN_DYA_PMW3610_SETTINGS", kconfig)

    for relative in (
        "drivers/pmw3610_alt/src/pixart.h",
        "drivers/pmw3610_alt/src/pmw3610.c",
        "drivers/pmw3610_alt/src/pmw3610.h",
        "dts/bindings/pixart,pmw3610-alt.yml",
    ):
        if not (repo / relative).is_file():
            fail(f"missing restored pmw3610_alt file: {repo / relative}")
    if (repo / "src/dya_pmw3610_settings.c").exists():
        fail(f"obsolete custom PMW settings source remains: {repo / 'src/dya_pmw3610_settings.c'}")

    right_conf = repo / "boards/shields/zen/zen_right.conf"
    right_conf_text = read_text(right_conf)
    for setting in (
        "CONFIG_ZMK_SETTINGS_RPC=y",
        "CONFIG_ZMK_SETTINGS_RPC_STUDIO=y",
        "CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048",
    ):
        require(right_conf_text, setting, right_conf)

    left_conf = repo / "boards/shields/zen/zen_left.conf"
    require(read_text(left_conf), "CONFIG_ZMK_SETTINGS_RPC=y", left_conf)

    build_yaml = repo / "build.yaml"
    build_text = read_text(build_yaml)
    require(build_text, "dya-runtime-input-settings", build_yaml)
    reject(build_text, "dya-pmw3610-settings", build_yaml)

    old_snippet = repo / "snippets/dya-pmw3610-settings"
    if old_snippet.exists() and any(old_snippet.iterdir()):
        fail(f"obsolete snippet remains: {old_snippet}")

    dya_conf = repo / "snippets/dya-runtime-input-settings/dya-runtime-input-settings.conf"
    dya_conf_text = read_text(dya_conf)
    for setting in (
        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR_STUDIO_RPC=y",
        "CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE=256",
    ):
        require(dya_conf_text, setting, dya_conf)
    for setting in (
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS",
        "CONFIG_ZMK_PMW3610_STUDIO_RPC",
        "CONFIG_ZEN_DYA_PMW3610_SETTINGS",
    ):
        reject(dya_conf_text, setting, dya_conf)

    pmw_conf = repo / "snippets/input-trackball-pmw3610/input-trackball-pmw3610.conf"
    pmw_conf_text = read_text(pmw_conf)
    for setting in (
        "CONFIG_PMW3610_ALT=y",
        "CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS=1000",
        "CONFIG_PMW3610_ALT_INIT_RETRY_DELAY_MS=1000",
        "CONFIG_PMW3610_ALT_INIT_RETRY_MAX_ATTEMPTS=0",
        "CONFIG_PMW3610_ALT_REPORT_INTERVAL_MIN=15",
        "CONFIG_PMW3610_ALT_RUN_DOWNSHIFT_TIME_MS=3264",
    ):
        require(pmw_conf_text, setting, pmw_conf)
    reject(pmw_conf_text, "CONFIG_PMW3610=y", pmw_conf)

    right_overlay = repo / "boards/shields/zen/zen_right.overlay"
    right_overlay_text = read_text(right_overlay)
    for setting in (
        "require-prior-idle-ms = <300>;",
        "excluded-positions = <19 20 21 22 24 38>;",
        "<&zip_temp_layer 1 30000>;",
    ):
        require(right_overlay_text, setting, right_overlay)

    pmw_overlay = repo / "snippets/input-trackball-pmw3610/input-trackball-pmw3610.overlay"
    pmw_overlay_text = read_text(pmw_overlay)
    for setting in (
        'compatible = "pixart,pmw3610-alt";',
        "cpi = <800>;",
        "force-awake;",
    ):
        require(pmw_overlay_text, setting, pmw_overlay)
    for setting in ('compatible = "cormoran,pmw3610";', "settings-id"):
        reject(pmw_overlay_text, setting, pmw_overlay)

    dya_overlay = repo / "snippets/dya-runtime-input-settings/dya-runtime-input-settings.overlay"
    dya_overlay_text = read_text(dya_overlay)
    for setting in (
        "#include <input/processors/runtime-input-processor.dtsi>",
        "&mouse_runtime_input_processor {",
        "scale-multiplier = <1>;",
        "scale-divisor = <1>;",
        "&scroll_runtime_input_processor {",
        "scale-divisor = <40>;",
        "<&pmw_gesture_2_processor>,",
        "<&pmw_gesture_1_processor>,",
        "<&zip_temp_layer 1 30000>,",
        "<&mouse_runtime_input_processor>;",
        "<&scroll_runtime_input_processor>;",
    ):
        require(dya_overlay_text, setting, dya_overlay)
    if dya_overlay_text.count("track-remainders;") != 2:
        fail(f"{dya_overlay}: pointer and scroll processors must both track remainders")
    reject(dya_overlay_text, "temp-layer-enabled;", dya_overlay)

    base_dtsi = repo / "boards/shields/zen/zen.dtsi"
    if read_text(base_dtsi).count("threshold = <30>;") != 2:
        fail(f"{base_dtsi}: shared left-side gesture thresholds must remain 30")
    left_listeners = repo / "snippets/input-split-listener-left-all/input-split-listener-left-all.overlay"
    left_text = read_text(left_listeners)
    for setting in (
        "pmw_gesture_1_processor: pmw_gesture_1_processor {",
        "pmw_gesture_2_processor: pmw_gesture_2_processor {",
        "<&pmw_gesture_2_processor>,",
        "<&pmw_gesture_1_processor>,",
        "<&left_pmw3610_scroll_scaler 3 80>;",
    ):
        require(left_text, setting, left_listeners)
    if left_text.count("threshold = <40>;") != 2:
        fail(f"{left_listeners}: both PMW gesture thresholds must be 40")
    if left_text.count("<&gesture_2_processor>,") != 2 or left_text.count("<&gesture_1_processor>,") != 2:
        fail(f"{left_listeners}: PAW3222 and trackpad must keep the shared threshold-30 gestures")
    reject(left_text, "<&left_pmw3610_scroll_scaler 1 20>;", left_listeners)


def verify_binary(path: Path) -> None:
    if not path.is_file():
        fail(f"missing firmware/ELF: {path}")
    data = path.read_bytes()
    for identifier in REQUIRED_SUBSYSTEMS:
        if identifier not in data:
            fail(f"{path}: linked firmware is missing {identifier.decode()}")
    for identifier in FORBIDDEN_SUBSYSTEMS:
        if identifier in data:
            fail(f"{path}: removed subsystem is still linked: {identifier.decode()}")


def verify_build(build_dir: Path) -> None:
    config = build_dir / "zephyr/.config"
    config_text = read_text(config)
    for setting in (
        "CONFIG_ZMK_SETTINGS_RPC=y",
        "CONFIG_ZMK_SETTINGS_RPC_STUDIO=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR_STUDIO_RPC=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
        "CONFIG_PMW3610_ALT=y",
        "CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE=256",
    ):
        require(config_text, setting, config)
    for setting in (
        "CONFIG_PMW3610=y",
        "CONFIG_ZMK_PMW3610_STUDIO_RPC=y",
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y",
        "CONFIG_ZEN_DYA_PMW3610_SETTINGS=y",
    ):
        reject(config_text, setting, config)

    dts = build_dir / "zephyr/zephyr.dts"
    dts_text = " ".join(read_text(dts).split())
    for setting in (
        'compatible = "pixart,pmw3610-alt";',
        "cpi = < 0x320 >;",
        "require-prior-idle-ms = < 0x12c >;",
        "excluded-positions = < 0x13 0x14 0x15 0x16 0x18 0x26 >;",
        'processor-label = "mouse";',
        'processor-label = "scroll";',
        "scale-divisor = < 0x28 >;",
        "pmw_gesture_1_processor: pmw_gesture_1_processor",
        "pmw_gesture_2_processor: pmw_gesture_2_processor",
        "threshold = < 0x1e >;",
        "threshold = < 0x28 >;",
        "< &pmw_gesture_2_processor >, < &pmw_gesture_1_processor >, < &zip_temp_layer 0x1 0x7530 >, < &mouse_runtime_input_processor >;",
        "< &pmw_gesture_2_processor >, < &pmw_gesture_1_processor >, < &zip_temp_layer 0x1 0x1f4 >;",
        "< &zip_xy_to_scroll_mapper >, < &scroll_runtime_input_processor >;",
        "< &zip_xy_scaler 0x1 0x38 >, < &zip_xy_transform 0x3 >, < &zip_xy_to_scroll_mapper >, < &left_pmw3610_scroll_scaler 0x3 0x50 >;",
    ):
        require(dts_text, setting, dts)
    reject(dts_text, 'compatible = "cormoran,pmw3610";', dts)
    reject(dts_text, "temp-layer-enabled;", dts)

    mouse_match = re.search(
        r'mouse_runtime_input_processor: mouse_runtime_input_processor \{(.*?)\};',
        dts_text,
    )
    if mouse_match is None:
        fail(f"{dts}: missing mouse runtime processor node")
    mouse_node = mouse_match.group(1)
    require(mouse_node, "scale-multiplier = < 0x1 >;", dts)
    require(mouse_node, "scale-divisor = < 0x1 >;", dts)
    require(mouse_node, "track-remainders;", dts)

    scroll_match = re.search(
        r'scroll_runtime_input_processor: scroll_runtime_input_processor \{(.*?)\};',
        dts_text,
    )
    if scroll_match is None:
        fail(f"{dts}: missing scroll runtime processor node")
    scroll_node = scroll_match.group(1)
    require(scroll_node, "scale-multiplier = < 0x1 >;", dts)
    require(scroll_node, "scale-divisor = < 0x28 >;", dts)
    require(scroll_node, "track-remainders;", dts)

    ninja = build_dir / "build.ninja"
    ninja_text = read_text(ninja)
    for source in (
        "drivers/pmw3610_alt/src/pmw3610.c",
        "zmk-module-settings-rpc/src/studio/settings_rpc_handler.c",
        "zmk-module-runtime-input-processor/src/studio/custom_handler.c",
        "zmk-feature-custom-settings/src/studio/custom_settings_handler.c",
    ):
        require(ninja_text, source, ninja)
    for source in (
        "zmk-driver-pmw3610-with-custom-studio-rpc/src/pmw3610.c",
        "zmk-driver-pmw3610-with-custom-studio-rpc/src/studio/pmw3610_handler.c",
        "zmk-driver-pmw3610-with-custom-studio-rpc/src/settings/pmw3610_settings.c",
        "src/dya_pmw3610_settings.c",
    ):
        reject(ninja_text, source, ninja)

    verify_binary(build_dir / "zephyr/zmk.elf")


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
    print("DYA build contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"DYA build contract: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
