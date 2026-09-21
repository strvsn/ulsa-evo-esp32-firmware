#!/usr/bin/env python3
"""Verify that a public snapshot is self-contained, sanitized, and buildable."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

from artifact_privacy import (
    FORBIDDEN_SOURCE_MARKERS,
    assert_artifact_private_safe,
    private_repository_markers,
)


ROOT = Path(__file__).resolve().parents[1]
PROFILES = ("m5stamp-c3u", "m5stamp-c3u-initial")
REQUIRED_PATHS = (
    "README.md",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "platformio.ini",
    "src/main.cpp",
    "src/main_initial.cpp",
    "include/system/firmware_version.h",
    "config/ulsa_evo_compatibility_contract.json",
    "config/license_compliance.json",
    "config/third_party_components.json",
    "docs/CUSTOM_FIRMWARE_COMPATIBILITY.md",
    "scripts/verify_ulsa_evo_compatibility.py",
    "scripts/artifact_privacy.py",
    "scripts/restore_public_demo.py",
    "scripts/generate_compliance_artifacts.py",
    "public-source-manifest.json",
    "test/artifact_privacy_test.py",
)
FORBIDDEN_PATH_PREFIXES = (
    "internal/",
    "public_templates/",
    "docs/research/",
    "osc/",
    "power_consumption/",
    "firmware/",
)
PUBLIC_NATIVE_TESTS = (
    ("test/button_click_gesture_test.cpp", ()),
    ("test/button_gesture_classifier_test.cpp", ()),
    ("test/boot_recovery_hold_test.cpp", ()),
    ("test/portal_stop_deadline_test.cpp", ()),
    ("test/system_ui_state_test.cpp", ()),
    ("test/update_coordinator_test.cpp", ("src/system/update_coordinator.cpp",)),
    ("test/esp32_firmware_identity_test.cpp", ()),
    ("test/esp32_ota_image_validator_test.cpp", ("src/ota/esp32_ota_image_validator.cpp",)),
    ("test/usb_command_line_input_test.cpp", ()),
    ("test/led_brightness_levels_test.cpp", ()),
    ("test/sd_logging_led_effect_test.cpp", ()),
    ("test/sd_log_sample_gate_test.cpp", ()),
    ("test/sd_storage_contract_test.cpp", ()),
    ("test/rtc_validation_test.cpp", ()),
    ("test/stm32_version_identity_test.cpp", ("src/stm32_update/stm32_version_identity.cpp",)),
    ("test/stm32_target_identity_test.cpp", ("src/stm32_update/stm32_target_identity.cpp",)),
)


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True
    )
    return sorted(value.decode("utf-8") for value in result.stdout.split(b"\0") if value)


def verify_tree() -> None:
    paths = tracked_files()
    missing = [path for path in REQUIRED_PATHS if path not in paths]
    if missing:
        raise RuntimeError("missing required public paths: " + ", ".join(missing))
    forbidden_paths = [
        path for path in paths
        if path in {"AGENTS.md", "platformio.internal.ini"}
        or path.startswith(FORBIDDEN_PATH_PREFIXES)
    ]
    if forbidden_paths:
        raise RuntimeError("forbidden public paths: " + ", ".join(forbidden_paths))
    for relative in paths:
        path = ROOT / relative
        if not path.is_file() or path.is_symlink():
            raise RuntimeError(f"public path must be a regular file: {relative}")
        payload = path.read_bytes()
        forbidden_word_mark = b"blue" + b"tooth"
        if (forbidden_word_mark in relative.lower().encode("utf-8") or
                forbidden_word_mark in payload.lower()):
            raise RuntimeError(
                f"public source must use BLE terminology and no wireless brand word mark: {relative}"
            )
        for marker in (*FORBIDDEN_SOURCE_MARKERS, *private_repository_markers()):
            if marker in payload:
                raise RuntimeError(
                    f"forbidden public content in {relative}: {marker.decode('ascii', 'replace')}"
                )


def native_test(source: str, extra_sources: tuple[str, ...]) -> None:
    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        raise RuntimeError("a C++17 compiler is required for public regression tests")
    with tempfile.TemporaryDirectory(prefix="ulsa-public-test-") as temporary:
        output = Path(temporary) / Path(source).stem
        run([compiler, "-std=c++17", "-Iinclude", source, *extra_sources, "-o", str(output)])
        run([str(output)])


def verify_binaries() -> None:
    contract = json.loads(
        (ROOT / "config" / "ulsa_evo_compatibility_contract.json").read_text(
            encoding="utf-8"
        )
    )
    budgets = contract["esp32Ota"]["firmwareBudgetsBytes"]
    for profile in PROFILES:
        build_dir = ROOT / ".pio" / "build" / profile
        artifacts = {
            name: build_dir / name
            for name in ("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin")
        }
        missing = [name for name, path in artifacts.items() if not path.is_file()]
        if missing:
            raise RuntimeError(
                f"missing public build artifacts for {profile}: " + ", ".join(missing)
            )
        firmware = artifacts["firmware.bin"]
        if firmware.stat().st_size > int(budgets[profile]):
            raise RuntimeError(
                f"{profile} firmware exceeds compatibility budget: "
                f"{firmware.stat().st_size} > {budgets[profile]}"
            )
        for path in artifacts.values():
            assert_artifact_private_safe(
                path,
                allow_ci_runner_paths=path.suffix.lower() == ".elf",
            )


def verify_compliance(*, require_resolved: bool) -> None:
    # Generate into an ephemeral directory so the public source remains a
    # clean source snapshot while CI still checks the complete Release bundle.
    from generate_compliance_artifacts import (
        generate_release_compliance,
        verify_compliance_output,
    )

    with tempfile.TemporaryDirectory(prefix="ulsa-public-compliance-") as temporary:
        output = Path(temporary)
        generate_release_compliance(output, require_resolved=require_resolved)
        verify_compliance_output(output, require_resolved=require_resolved)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    verify_tree()
    run([sys.executable, "scripts/sync_esp32_firmware_version.py"])
    run([sys.executable, "scripts/verify_ulsa_evo_compatibility.py"])
    run([sys.executable, "test/artifact_privacy_test.py"])
    for source, extra_sources in PUBLIC_NATIVE_TESTS:
        native_test(source, extra_sources)
    compiler = os.environ.get("CXX") or shutil.which("c++")
    with tempfile.TemporaryDirectory(prefix="ulsa-sd-worker-") as temporary:
        executable = str(Path(temporary) / "worker-test")
        run([compiler, "-std=c++17", "-pthread", "-DSD_LOG_MOUNT_POINT=sdTestMountPoint()",
             "-Itest/sd_host_mocks", "-Iinclude", "-Iinclude/storage",
             "test/sd_logger_worker_test.cpp", "src/storage/sd_logger.cpp",
             "src/storage/sd_buffer_ops.cpp", "src/storage/sd_file_ops.cpp",
             "src/storage/sd_storage_worker.cpp", "-o", executable])
        run([executable])
    if args.build:
        for profile in PROFILES:
            run(["pio", "run", "-e", profile])
        verify_binaries()
    verify_compliance(require_resolved=args.build)
    print("Public source verification passed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Public source verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
