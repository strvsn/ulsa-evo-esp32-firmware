#!/usr/bin/env python3
"""Build and restore the unmodified public-snapshot Demo firmware over USB."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "m5stamp-c3u"


def run(command: list[str], *, dry_run: bool) -> None:
    print("+ " + " ".join(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=ROOT, check=True)


def require_clean_snapshot() -> None:
    if not (ROOT / ".git").exists():
        raise RuntimeError(
            "A Git clone of an immutable public snapshot is required for source restoration."
        )
    result = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    if result.stdout.strip():
        raise RuntimeError(
            "Tracked files are modified. Use a fresh public snapshot clone for restoration."
        )


def restore_commands(port: str, *, full_erase: bool) -> list[list[str]]:
    commands = [
        [sys.executable, "scripts/verify_public_source.py"],
        ["pio", "run", "-e", ENVIRONMENT],
    ]
    if full_erase:
        commands.append([
            "pio", "run", "-e", ENVIRONMENT, "-t", "erase", "--upload-port", port
        ])
    commands.append([
        "pio", "run", "-e", ENVIRONMENT, "-t", "upload", "--upload-port", port
    ])
    return commands


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32-C3 USB serial port")
    parser.add_argument(
        "--full-erase",
        action="store_true",
        help="erase ESP32 flash before restoring bootloader, partitions, and Demo",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    require_clean_snapshot()
    if shutil.which("pio") is None and not args.dry_run:
        raise RuntimeError("PlatformIO Core 6.1.18 is required and pio is not on PATH.")
    if args.full_erase:
        print(
            "Full restore selected: ESP32 NVS/settings will be erased. "
            "STM32 firmware/EEPROM and the SD card are not written by this script."
        )
    for command in restore_commands(args.port, full_erase=args.full_erase):
        run(command, dry_run=args.dry_run)
    print("Public-snapshot Demo restore completed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Demo restore failed: {error}", file=sys.stderr)
        print(
            "If automatic reset cannot enter the ROM loader, disconnect USB, hold the "
            "device button (GPIO9), reconnect USB while holding it, then release the "
            "button after the ROM serial port appears.",
            file=sys.stderr,
        )
        raise SystemExit(1)
