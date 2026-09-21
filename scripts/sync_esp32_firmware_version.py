#!/usr/bin/env python3
"""Synchronize the generated ESP32 firmware version header with its config."""

from __future__ import annotations

import argparse

from esp32_firmware_identity import CONFIG_PATH, HEADER_PATH, ROOT, load_identity, render_header


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    identity = load_identity(ROOT)
    path = ROOT / HEADER_PATH
    expected = render_header(identity)
    actual = path.read_text(encoding="utf-8") if path.is_file() else ""
    if actual == expected:
        print(
            "ESP32 firmware version sources are synchronized: "
            f"{identity.version} / {identity.revision}"
        )
        return
    if not args.write:
        raise RuntimeError(
            f"{HEADER_PATH} differs from {CONFIG_PATH}; run with --write"
        )
    path.write_text(expected, encoding="utf-8")
    print(f"Updated {HEADER_PATH}: {identity.version} / {identity.revision}")


if __name__ == "__main__":
    main()
