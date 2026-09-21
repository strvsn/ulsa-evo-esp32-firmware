#!/usr/bin/env python3
"""Generate the shared IANA zone/link catalog from AceTime zonedbx."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "config" / "timezone_catalog.json"
ACETIME_VERSION = "4.1.0"
ACETIME_COMMIT = "3dc2f58811e153e02bd16161da579dd201746372"
TZDB_VERSION = "2025b"
EXPECTED_COUNT = 597
ZONE_PATTERN = re.compile(
    r"^const uint32_t kZoneId[A-Za-z0-9_]+ = (0x[0-9a-f]{8}); // (\S+)$",
    re.MULTILINE,
)


def locate_zone_infos(explicit_root: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit_root is not None:
        candidates.append(explicit_root / "src" / "zonedbx" / "zone_infos.h")
    candidates.extend(
        ROOT / ".pio" / "libdeps" / environment / "AceTime" / "src" / "zonedbx" / "zone_infos.h"
        for environment in ("m5stamp-c3u", "m5stamp-c3u-initial")
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "AceTime zonedbx/zone_infos.h was not found; build a profile or pass --ace-time-root"
    )


def build_catalog(zone_infos: Path) -> dict:
    source = zone_infos.read_text(encoding="utf-8")
    if "--tz_version 2025b" not in source or "Supported Zones: 597" not in source:
        raise RuntimeError("AceTime zone_infos.h is not the approved TZDB 2025b zonedbx registry")
    entries = [
        {"name": name, "zoneId": int(zone_id, 16)}
        for zone_id, name in ZONE_PATTERN.findall(source)
    ]
    if len(entries) != EXPECTED_COUNT:
        raise RuntimeError(f"Expected {EXPECTED_COUNT} zones/links, found {len(entries)}")
    entries.sort(key=lambda item: item["name"])
    names = [item["name"] for item in entries]
    ids = [item["zoneId"] for item in entries]
    if len(set(names)) != len(names) or len(set(ids)) != len(ids):
        raise RuntimeError("AceTime catalog contains a duplicate name or zone ID")
    required = {
        "Africa/Casablanca",
        "America/New_York",
        "Asia/Kathmandu",
        "Asia/Tokyo",
        "Australia/Lord_Howe",
        "Europe/London",
        "Pacific/Chatham",
        "UTC",
    }
    missing = sorted(required.difference(names))
    if missing:
        raise RuntimeError("Required timezone fixtures are missing: " + ", ".join(missing))
    return {
        "schemaVersion": 1,
        "source": "AceTime/zonedbx/kZoneAndLinkRegistry",
        "aceTimeVersion": ACETIME_VERSION,
        "aceTimeCommit": ACETIME_COMMIT,
        "tzdbVersion": TZDB_VERSION,
        "tzdbYear": 2025,
        "tzdbRevisionLetter": "b",
        "startYear": 2000,
        "untilYear": 2200,
        "zoneAndLinkCount": len(entries),
        "zones": entries,
    }


def encode_catalog(catalog: dict) -> bytes:
    return (json.dumps(catalog, ensure_ascii=True, indent=2) + "\n").encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ace-time-root", type=Path)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    expected = encode_catalog(build_catalog(locate_zone_infos(args.ace_time_root)))
    if args.check:
        if not args.output.is_file() or args.output.read_bytes() != expected:
            raise RuntimeError(f"Timezone catalog is stale: {args.output}")
        print(f"Timezone catalog verified: {EXPECTED_COUNT} zones/links")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(expected)
    print(f"Timezone catalog generated: {args.output} ({EXPECTED_COUNT} zones/links)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
