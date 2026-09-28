#!/usr/bin/env python3
"""Canonical ESP32 firmware identity shared by build and Release tooling."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = Path("config/esp32_firmware_version.json")
HEADER_PATH = Path("include/system/firmware_version.h")
VERSION_CODE_MARKER = 0x7E000000
DESCRIPTOR_MAGIC = b"ULSAE32V"
DESCRIPTOR_SCHEMA_VERSION = 1
DESCRIPTOR_STRUCT = struct.Struct("<8sBBHII41s12s65s")
PROFILE_CODES = {"demo": 1, "initial": 2}
PROFILE_NAMES = {value: key for key, value in PROFILE_CODES.items()}
DIRTY_FLAG = 0x0001
CANONICAL_VERSION = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
COMMIT_SHA = re.compile(r"[0-9a-f]{40}")


@dataclass(frozen=True)
class Esp32FirmwareIdentity:
    version: str
    major: int
    minor: int
    patch: int
    version_code: int
    revision: int

    @property
    def release_tag(self) -> str:
        return f"esp32-fw-v{self.version}-r{self.revision}"


@dataclass(frozen=True)
class Esp32ArtifactIdentity:
    profile: str
    version: str
    version_code: int
    revision: int
    commit: str
    dirty: bool
    build_contract_sha256: str


def parse_canonical_version(value: str) -> tuple[int, int, int]:
    match = CANONICAL_VERSION.fullmatch(value)
    if not match:
        raise RuntimeError("version must be canonical MAJOR.MINOR.PATCH")
    components = tuple(int(component) for component in match.groups())
    if any(component > 255 for component in components):
        raise RuntimeError("version components must be 0..255")
    return components


def pack_version_code(version: str) -> int:
    major, minor, patch = parse_canonical_version(version)
    return VERSION_CODE_MARKER | major << 16 | minor << 8 | patch


def decode_version_code(code: int) -> tuple[int, int, int]:
    if not isinstance(code, int) or code < 0 or code > 0xFFFFFFFF:
        raise RuntimeError("versionCode must fit uint32")
    if code & 0xFF000000 != VERSION_CODE_MARKER:
        raise RuntimeError("versionCode marker is invalid")
    return (code >> 16 & 0xFF, code >> 8 & 0xFF, code & 0xFF)


def format_version_code(code: int) -> str:
    return ".".join(str(component) for component in decode_version_code(code))


def load_identity(root: Path = ROOT) -> Esp32FirmwareIdentity:
    raw = json.loads((root / CONFIG_PATH).read_text(encoding="utf-8"))
    if raw.get("schemaVersion") != 1:
        raise RuntimeError("unsupported ESP32 firmware version config schema")
    if set(raw) != {"schemaVersion", "version", "revision"}:
        raise RuntimeError("ESP32 firmware version config fields are incomplete or unexpected")
    version = raw.get("version")
    if not isinstance(version, str):
        raise RuntimeError("version must be a string")
    major, minor, patch = parse_canonical_version(version)
    revision = raw.get("revision")
    if not isinstance(revision, int) or isinstance(revision, bool) or not 0 < revision <= 0xFFFFFFFF:
        raise RuntimeError("revision must be a positive uint32")
    return Esp32FirmwareIdentity(
        version=version,
        major=major,
        minor=minor,
        patch=patch,
        version_code=pack_version_code(version),
        revision=revision,
    )


def render_header(identity: Esp32FirmwareIdentity) -> str:
    return f"""#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#include <stdint.h>

#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR {identity.major}U
#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR {identity.minor}U
#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH {identity.patch}U
#define ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME \"{identity.version}\"
#define ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE 0x{identity.version_code:08X}UL
#define ULSA_EVO_ESP32_FIRMWARE_REVISION {identity.revision}UL
#define ULSA_EVO_ESP32_FIRMWARE_VERSION ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME

#define ULSA_EVO_MANUFACTURER_NAME \"StratoVision LLC\"
#define ULSA_EVO_MODEL_NUMBER \"ULSA EVO\"

struct Esp32SemanticVersion {{
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
}};

static inline uint32_t packEsp32FirmwareVersionCode(const Esp32SemanticVersion& version) {{
  return 0x7E000000UL |
         (static_cast<uint32_t>(version.major) << 16U) |
         (static_cast<uint32_t>(version.minor) << 8U) |
         static_cast<uint32_t>(version.patch);
}}

static inline bool decodeEsp32FirmwareVersionCode(uint32_t code,
                                                  Esp32SemanticVersion& out) {{
  if ((code & 0xFF000000UL) != 0x7E000000UL) return false;
  out.major = static_cast<uint8_t>((code >> 16U) & 0xFFU);
  out.minor = static_cast<uint8_t>((code >> 8U) & 0xFFU);
  out.patch = static_cast<uint8_t>(code & 0xFFU);
  return true;
}}

static_assert(ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE ==
                  (0x7E000000UL |
                   (ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR << 16U) |
                   (ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR << 8U) |
                   ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH),
              \"ESP32 firmware versionCode differs from SemVer\");

#endif // FIRMWARE_VERSION_H
"""


def build_contract_sha256(root: Path = ROOT) -> str:
    return hashlib.sha256(
        (root / "config" / "firmware_build_contract.json").read_bytes()
    ).hexdigest()


def parse_artifact_identity(payload: bytes) -> Esp32ArtifactIdentity:
    offsets = [
        index
        for index in range(len(payload))
        if payload.startswith(DESCRIPTOR_MAGIC, index)
    ]
    if len(offsets) != 1:
        raise RuntimeError(f"expected one ESP32 identity descriptor, found {len(offsets)}")
    end = offsets[0] + DESCRIPTOR_STRUCT.size
    if end > len(payload):
        raise RuntimeError("ESP32 identity descriptor is truncated")
    (
        magic,
        schema,
        profile_code,
        flags,
        version_code,
        revision,
        commit_raw,
        version_raw,
        build_contract_raw,
    ) = DESCRIPTOR_STRUCT.unpack_from(payload, offsets[0])
    if magic != DESCRIPTOR_MAGIC or schema != DESCRIPTOR_SCHEMA_VERSION:
        raise RuntimeError("ESP32 identity descriptor header is invalid")
    profile = PROFILE_NAMES.get(profile_code)
    if profile is None:
        raise RuntimeError("ESP32 identity descriptor profile is invalid")
    if flags & ~DIRTY_FLAG:
        raise RuntimeError("ESP32 identity descriptor flags are invalid")
    commit = commit_raw.split(b"\0", 1)[0].decode("ascii")
    if not COMMIT_SHA.fullmatch(commit):
        raise RuntimeError("ESP32 identity descriptor commit is invalid")
    version = version_raw.split(b"\0", 1)[0].decode("ascii")
    if format_version_code(version_code) != version:
        raise RuntimeError("ESP32 identity descriptor version fields disagree")
    build_contract = build_contract_raw.split(b"\0", 1)[0].decode("ascii")
    if not re.fullmatch(r"[0-9a-f]{64}", build_contract):
        raise RuntimeError("ESP32 identity descriptor build contract digest is invalid")
    return Esp32ArtifactIdentity(
        profile=profile,
        version=version,
        version_code=version_code,
        revision=revision,
        commit=commit,
        dirty=bool(flags & DIRTY_FLAG),
        build_contract_sha256=build_contract,
    )


def read_artifact_identity(path: Path) -> Esp32ArtifactIdentity:
    return parse_artifact_identity(path.read_bytes())


def validate_artifact_identity(
    artifact: Esp32ArtifactIdentity,
    configured: Esp32FirmwareIdentity,
    *,
    profile: str,
    commit: str,
    dirty: bool,
    build_contract: str,
    allow_dirty: bool,
) -> None:
    expected = {
        "profile": profile,
        "version": configured.version,
        "version_code": configured.version_code,
        "revision": configured.revision,
        "commit": commit,
        "build_contract_sha256": build_contract,
    }
    actual = {
        "profile": artifact.profile,
        "version": artifact.version,
        "version_code": artifact.version_code,
        "revision": artifact.revision,
        "commit": artifact.commit,
        "build_contract_sha256": artifact.build_contract_sha256,
    }
    if actual != expected:
        raise RuntimeError(f"ESP32 artifact identity mismatch: expected={expected}, actual={actual}")
    if artifact.dirty != dirty:
        raise RuntimeError(
            "ESP32 artifact dirty flag differs from the current source state: "
            f"artifact={artifact.dirty}, source={dirty}"
        )
    if artifact.dirty and not allow_dirty:
        raise RuntimeError("ESP32 artifact identity was built from a dirty worktree")
