#!/usr/bin/env python3
"""Save PlatformIO memory usage for a selected firmware environment."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_ENV = "m5stamp-c3u"
SCRIPT_PATH = globals().get("__file__")
ROOT = Path(SCRIPT_PATH).resolve().parents[1] if SCRIPT_PATH else Path.cwd()
PROGRAM_SECTIONS = {".iram0.text", ".iram0.vectors", ".dram0.data", ".flash.text", ".flash.rodata"}
RAM_SECTIONS = {".dram0.data", ".dram0.bss", ".noinit"}
RAM_TOTAL = 327_680


def repo_root() -> Path:
    return ROOT


def find_size_tool() -> Path:
    configured = os.environ.get("ULSA_RISCV_SIZE")
    candidates = [
        Path(configured).expanduser() if configured else None,
        Path.home() / ".platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-size",
        Path(shutil.which("riscv32-esp-elf-size")) if shutil.which("riscv32-esp-elf-size") else None,
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    raise FileNotFoundError("riscv32-esp-elf-size was not found")


def read_section_sizes(elf_path: Path) -> dict[str, int]:
    result = subprocess.run(
        [str(find_size_tool()), "-A", "-d", str(elf_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    sections: dict[str, int] = {}
    for line in result.stdout.splitlines():
        match = re.match(r"^(\.\S+)\s+(\d+)\s+", line)
        if match:
            sections[match.group(1)] = int(match.group(2))
    return sections


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def application_partition_size() -> int:
    for raw_line in (repo_root() / "partitions_ulsa_stm32pkg.csv").read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) >= 5 and fields[0] == "app0":
            return parse_int(fields[4])
    raise RuntimeError("app0 partition size was not found")


def resolve_build_dir(env_name: str, build_dir: str | Path | None) -> Path:
    path = Path(build_dir) if build_dir is not None else Path(".pio") / "build" / env_name
    return path if path.is_absolute() else repo_root() / path


def save_build_info(
    env_name: str = DEFAULT_ENV,
    build_dir: str | Path | None = None,
) -> Path:
    resolved_build_dir = resolve_build_dir(env_name, build_dir)
    elf_path = resolved_build_dir / "firmware.elf"
    if not elf_path.is_file():
        raise FileNotFoundError(f"Build artifact not found: {elf_path}")
    sections = read_section_sizes(elf_path)
    flash_used = sum(sections.get(name, 0) for name in PROGRAM_SECTIONS)
    ram_used = sum(sections.get(name, 0) for name in RAM_SECTIONS)
    flash_total = application_partition_size()
    ram_total = RAM_TOTAL
    build_info = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "environment": env_name,
        "flash_used": flash_used,
        "flash_total": flash_total,
        "flash_percent": flash_used / flash_total * 100,
        "ram_used": ram_used,
        "ram_total": ram_total,
        "ram_percent": ram_used / ram_total * 100,
    }

    output_path = resolved_build_dir / "build_info.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(build_info, indent=2) + "\n", encoding="utf-8")
    print(
        f"Saved {env_name} build info: Flash {flash_used:,}/{flash_total:,} bytes, "
        f"RAM {ram_used:,}/{ram_total:,} bytes"
    )
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default=DEFAULT_ENV, help="PlatformIO environment")
    args = parser.parse_args()
    save_build_info(args.env)


try:
    Import("env")
except NameError:
    env = None

if env is not None:
    def save_build_info_action(source, target, env):
        save_build_info(env.subst("$PIOENV"), env.subst("$BUILD_DIR"))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", save_build_info_action)
elif __name__ == "__main__":
    main()
