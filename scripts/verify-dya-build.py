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
    b"cormoran__pmw3610",
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
    for name, revision in (
        ("zmk-module-settings-rpc", "78f86df9e6c5edaf57bef3ccbd7f360cfdf49291"),
        ("zmk-module-runtime-input-processor", "43618985f8c9d5457cc333b7ca0733f2d361911e"),
        (
            "zmk-driver-pmw3610-with-custom-studio-rpc",
            "5c34ea0eec246a1c986111417cd779b53144629a",
        ),
    ):
        pattern = rf"- name: {re.escape(name)}\s+remote: cormoran\s+revision: {revision}"
        if re.search(pattern, west_text) is None:
            fail(f"{west}: {name} is not pinned to {revision}")

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

    dya_conf = repo / "snippets/dya-pmw3610-settings/dya-pmw3610-settings.conf"
    dya_conf_text = read_text(dya_conf)
    for setting in (
        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR_STUDIO_RPC=y",
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y",
        "CONFIG_ZMK_PMW3610_STUDIO_RPC=y",
        "CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE=256",
    ):
        require(dya_conf_text, setting, dya_conf)

    pmw_conf = repo / "snippets/input-trackball-pmw3610/input-trackball-pmw3610.conf"
    pmw_conf_text = read_text(pmw_conf)
    for setting in (
        "CONFIG_PMW3610=y",
        "CONFIG_PMW3610_INIT_POWER_UP_EXTRA_DELAY_MS=1000",
        "CONFIG_PMW3610_REPORT_INTERVAL_MIN=15",
        "CONFIG_PMW3610_RUN_DOWNSHIFT_TIME_MS=3264",
    ):
        require(pmw_conf_text, setting, pmw_conf)
    reject(pmw_conf_text, "CONFIG_PMW3610_ALT=y", pmw_conf)

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
        'compatible = "cormoran,pmw3610";',
        "cpi = <600>;",
        'settings-id = "trackball";',
    ):
        require(pmw_overlay_text, setting, pmw_overlay)

    dya_overlay = repo / "snippets/dya-pmw3610-settings/dya-pmw3610-settings.overlay"
    dya_overlay_text = read_text(dya_overlay)
    for setting in (
        "#include <input/processors/runtime-input-processor.dtsi>",
        "scale-divisor = <30>;",
        "<&gesture_2_processor>,",
        "<&gesture_1_processor>,",
        "<&zip_temp_layer 1 30000>,",
        "<&mouse_runtime_input_processor>;",
        "<&scroll_runtime_input_processor>;",
    ):
        require(dya_overlay_text, setting, dya_overlay)


def verify_binary(path: Path) -> None:
    if not path.is_file():
        fail(f"missing firmware/ELF: {path}")
    data = path.read_bytes()
    for identifier in REQUIRED_SUBSYSTEMS:
        if identifier not in data:
            fail(f"{path}: linked firmware is missing {identifier.decode()}")
    if b"zen__pmw3610_settings" in data:
        fail(f"{path}: obsolete zen__pmw3610_settings subsystem is still linked")


def verify_build(build_dir: Path) -> None:
    config = build_dir / "zephyr/.config"
    config_text = read_text(config)
    for setting in (
        "CONFIG_ZMK_SETTINGS_RPC=y",
        "CONFIG_ZMK_SETTINGS_RPC_STUDIO=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y",
        "CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR_STUDIO_RPC=y",
        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
        "CONFIG_ZMK_PMW3610_STUDIO_RPC=y",
        "CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y",
        "CONFIG_PMW3610=y",
    ):
        require(config_text, setting, config)
    reject(config_text, "CONFIG_PMW3610_ALT=y", config)

    dts = build_dir / "zephyr/zephyr.dts"
    dts_text = " ".join(read_text(dts).split())
    for setting in (
        'compatible = "cormoran,pmw3610";',
        "cpi = < 0x258 >;",
        'settings-id = "trackball";',
        "require-prior-idle-ms = < 0x12c >;",
        "excluded-positions = < 0x13 0x14 0x15 0x16 0x18 0x26 >;",
        "scale-divisor = < 0x1e >;",
        "< &gesture_2_processor >, < &gesture_1_processor >, < &zip_temp_layer 0x1 0x7530 >, < &mouse_runtime_input_processor >;",
        "< &zip_xy_to_scroll_mapper >, < &scroll_runtime_input_processor >;",
    ):
        require(dts_text, setting, dts)

    ninja = build_dir / "build.ninja"
    ninja_text = read_text(ninja)
    for source in (
        "zmk-module-settings-rpc/src/studio/settings_rpc_handler.c",
        "zmk-module-runtime-input-processor/src/studio/custom_handler.c",
        "zmk-feature-custom-settings/src/studio/custom_settings_handler.c",
        "zmk-driver-pmw3610-with-custom-studio-rpc/src/studio/pmw3610_handler.c",
        "zmk-driver-pmw3610-with-custom-studio-rpc/src/settings/pmw3610_settings.c",
    ):
        require(ninja_text, source, ninja)

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
