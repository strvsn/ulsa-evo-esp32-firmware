#!/usr/bin/env python3
"""Generate reproducible third-party notices, license bundles, and SBOMs.

The component contract is the human-maintained inventory.  The license
contract supplies immutable upstream license/notice sources, while this
module resolves the actual PlatformIO package and linker-map evidence used by
a build.  A formal release therefore cannot silently rely on an abbreviated
MIT-only notice or on a stale dependency list.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen

from esp32_firmware_identity import ROOT, load_identity


CONTRACT_PATH = ROOT / "config" / "third_party_components.json"
LICENSE_CONTRACT_PATH = ROOT / "config" / "license_compliance.json"
NOTICE_PATH = ROOT / "THIRD_PARTY_NOTICES.md"
PROFILES = ("demo", "initial")
PROFILE_ENVIRONMENTS = {"demo": "m5stamp-c3u", "initial": "m5stamp-c3u-initial"}
COMPONENT_FIELDS = {
    "name",
    "version",
    "packageVersion",
    "license",
    "source",
    "purl",
    "scope",
    "profiles",
}
COMPONENT_KEYS = {
    "PlatformIO Core": "platformio_core",
    "PlatformIO Espressif 32": "espressif32_platform",
    "Arduino-ESP32": "arduino_esp32",
    "GCC Toolchain for ESP32 RISC-V": "riscv32_toolchain",
    "esptool": "esptool",
    "M5GFX": "m5gfx",
    "M5Unified": "m5unified",
    "Adafruit NeoPixel": "adafruit_neopixel",
    "NimBLE-Arduino": "nimble_arduino",
    "AceCommon": "ace_common",
    "AceSorting": "ace_sorting",
    "AceTime": "ace_time",
    "IANA Time Zone Database": "iana_tzdb",
    "ESP-IDF bundled SDK": "esp_idf_bundled_sdk",
    "ESP-IDF newlib": "esp_idf_newlib",
}
PACKAGE_METADATA_PATTERNS = {
    "Arduino-ESP32": ("packages/framework-arduinoespressif32/package.json",),
    "esptool": ("packages/tool-esptoolpy@*/package.json", "packages/tool-esptoolpy/package.json"),
    "GCC Toolchain for ESP32 RISC-V": ("packages/toolchain-riscv32-esp/package.json",),
    "PlatformIO Espressif 32": (
        "platforms/espressif32/.piopm",
        "platforms/espressif32@*/.piopm",
        "platforms/espressif32/package.json",
        "platforms/espressif32@*/package.json",
    ),
    "M5GFX": ("libdeps/*/M5GFX/library.json", "libdeps/*/M5GFX/library.properties"),
    "M5Unified": ("libdeps/*/M5Unified/library.json", "libdeps/*/M5Unified/library.properties"),
    "Adafruit NeoPixel": ("libdeps/*/Adafruit NeoPixel/library.properties",),
    "NimBLE-Arduino": ("libdeps/*/NimBLE-Arduino/library.properties",),
    "AceCommon": ("libdeps/*/AceCommon/library.properties",),
    "AceSorting": ("libdeps/*/AceSorting/library.properties",),
    "AceTime": ("libdeps/*/AceTime/library.properties",),
}


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_components() -> list[dict]:
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    if contract.get("schemaVersion") != 1 or set(contract) != {"schemaVersion", "components"}:
        raise RuntimeError("unsupported third-party component contract")
    components = contract.get("components")
    if not isinstance(components, list) or not components:
        raise RuntimeError("third-party component list is empty")
    names = set()
    purls = set()
    for component in components:
        if not isinstance(component, dict) or set(component) != COMPONENT_FIELDS:
            raise RuntimeError("third-party component fields are incomplete or unexpected")
        if component["name"] in names or component["purl"] in purls:
            raise RuntimeError("third-party component names and purls must be unique")
        names.add(component["name"])
        purls.add(component["purl"])
        if component["scope"] not in {"build", "runtime"}:
            raise RuntimeError("third-party component scope must be build or runtime")
        profiles = component["profiles"]
        if not isinstance(profiles, list) or not profiles or not set(profiles) <= set(PROFILES):
            raise RuntimeError("third-party component profiles are invalid")
        for field in ("name", "version", "packageVersion", "license", "source", "purl"):
            if not isinstance(component[field], str) or not component[field].strip():
                raise RuntimeError(f"third-party component {field} is invalid")
        if component["name"] not in COMPONENT_KEYS:
            raise RuntimeError(f"component has no release key: {component['name']}")
    return components


def build_components(components: list[dict] | None = None) -> dict[str, str]:
    """Return release build metadata directly from the component contract."""

    selected = components if components is not None else load_components()
    return {
        COMPONENT_KEYS[component["name"]]: component["packageVersion"]
        for component in selected
    }


def component_contract_sha256() -> str:
    return sha256(CONTRACT_PATH)


def load_license_contract() -> dict[str, Any]:
    contract = json.loads(LICENSE_CONTRACT_PATH.read_text(encoding="utf-8"))
    if set(contract) != {"schemaVersion", "legalReview", "licenseFiles", "sourceOffers", "archiveComponents"}:
        raise RuntimeError("license compliance contract fields are incomplete or unexpected")
    if contract.get("schemaVersion") != 1:
        raise RuntimeError("unsupported license compliance contract")
    review = contract.get("legalReview")
    if not isinstance(review, dict) or review.get("status") not in {"pending", "approved"}:
        raise RuntimeError("license legalReview status must be pending or approved")
    files = contract.get("licenseFiles")
    if not isinstance(files, list) or not files:
        raise RuntimeError("license file inventory is empty")
    ids: set[str] = set()
    paths: set[str] = set()
    for item in files:
        expected = {"id", "component", "license", "kind", "path", "url", "sha256", "cacheName"}
        if not isinstance(item, dict) or not expected <= set(item):
            raise RuntimeError("license file inventory fields are incomplete")
        if item["id"] in ids or item["path"] in paths:
            raise RuntimeError("license file IDs and paths must be unique")
        ids.add(item["id"])
        paths.add(item["path"])
        relative = Path(item["path"])
        if relative.is_absolute() or ".." in relative.parts or not item["path"].startswith("LICENSES/"):
            raise RuntimeError(f"license file path is outside LICENSES: {item['path']}")
        if not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
            raise RuntimeError(f"license file SHA-256 is invalid: {item['id']}")
        if item["kind"] not in {"license", "notice", "package-license"}:
            raise RuntimeError(f"license file kind is invalid: {item['id']}")
        for field in ("id", "component", "license", "path", "url", "cacheName"):
            if not isinstance(item[field], str) or not item[field].strip():
                raise RuntimeError(f"license file {field} is invalid: {item['id']}")
        if not item["url"].startswith("https://"):
            raise RuntimeError(f"license file source must use HTTPS: {item['id']}")
        if Path(item["cacheName"]).name != item["cacheName"]:
            raise RuntimeError(f"license cache name must be a basename: {item['id']}")
    offers = contract.get("sourceOffers")
    if not isinstance(offers, dict) or offers.get("writtenOfferRequired") is not True:
        raise RuntimeError("corresponding-source offer must be required")
    archive = contract.get("archiveComponents")
    if not isinstance(archive, dict) or not isinstance(archive.get("default"), str):
        raise RuntimeError("archive component default is invalid")
    if not isinstance(archive.get("overrides"), dict):
        raise RuntimeError("archive component overrides are invalid")
    archive_licenses = archive.get("archiveLicenses")
    if not isinstance(archive_licenses, dict):
        raise RuntimeError("archive license bindings are invalid")
    known_file_ids = {item["id"] for item in files}
    for archive_name, license_ids in archive_licenses.items():
        if not isinstance(archive_name, str) or not isinstance(license_ids, list):
            raise RuntimeError("archive license binding fields are invalid")
        if not all(isinstance(value, str) and value in known_file_ids for value in license_ids):
            raise RuntimeError(f"archive license binding references an unknown file: {archive_name}")
    return contract


def assert_legal_review_approved() -> None:
    """Stop a formal distribution until an authorized legal review exists."""

    review = load_license_contract()["legalReview"]
    if review.get("status") != "approved":
        raise RuntimeError(
            "formal Release is on hold: license legal review is not approved"
        )


def render_notice(components: list[dict]) -> str:
    rows = "\n".join(
        "| {name} | `{version}` | `{packageVersion}` | `{license}` | {profiles} | [source]({source}) |".format(
            **{**component, "profiles": ", ".join(component["profiles"])},
        )
        for component in components
    )
    return f"""# Third-Party Notices / 第三者ソフトウェア通知

## 日本語

ULSA EVO ESP32ファームウェアのプロジェクト部分は[MIT License](LICENSE)で提供します。ビルドとリンクに含まれる第三者ソフトウェアには、それぞれのライセンス条件が適用されます。

| Component | Upstream version | PlatformIO package version | License | Profiles | Source |
|---|---:|---:|---|---|---|
{rows}

Release asset `ulsa-evo-esp32-license-bundle.tar.gz`には、LGPL本文、NimBLE NOTICE／tinycrypt帰属表示、ESP-IDFのCOPYRIGHT一覧を含む、契約に対して解決した全ライセンス本文とNOTICEを同梱します。bundle manifestには各ファイルの取得元URLとSHA-256を記録します。

表はパッケージ単位の情報です。再現性のためビルドツールも記載しますが、`firmware.bin`へ自動的に含まれるとは限りません。runtime libraryはprofileのlinker mapに従います。Arduino-ESP32とESP-IDF bundled SDKには追加のコンポーネント条件があるため、bundleの本文を確認してください。

LGPL対象コンポーネントの対応ソースと再リンク手順は、`docs/OPEN_SOURCE_LICENSES.md`に記載します。プロジェクトのMIT Licenseを依存ソフトウェアのライセンスの代わりに扱わないでください。

## English

ULSA EVO ESP32 firmware is distributed under the project [MIT License](LICENSE). It builds with and links third-party software governed by its own license terms.

The release asset `ulsa-evo-esp32-license-bundle.tar.gz` contains the complete license and notice texts resolved for this contract, including LGPL terms, the NimBLE NOTICE and tinycrypt attribution, and the bundled ESP-IDF COPYRIGHT inventory. The bundle manifest records the source URL and SHA-256 of every file.

The table records package-level metadata. Build-only tools are listed for reproducibility but are not automatically part of `firmware.bin`; runtime libraries are linked according to the profile linker map. Arduino-ESP32 and the bundled ESP-IDF components may contain additional component-specific terms, which are included in the bundle and remain authoritative at the pinned upstream revision.

This inventory is technical evidence and not a substitute for legal review. A formal Release remains on hold until the legal review status in `config/license_compliance.json` is approved by an authorized reviewer.
"""


def _cyclonedx_license(value: str) -> dict[str, str]:
    known_spdx = {
        "Apache-2.0",
        "BSD-2-Clause",
        "BSD-3-Clause",
        "GPL-2.0-or-later",
        "ISC",
        "LGPL-2.1-or-later",
        "LGPL-3.0-only",
        "MIT",
    }
    return {"id": value} if value in known_spdx else {"name": value}


def sbom_for_profile(profile: str, components: list[dict]) -> dict:
    identity = load_identity(ROOT)
    selected = [component for component in components if profile in component["profiles"]]
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {
            "component": {
                "type": "firmware",
                "name": f"ULSA EVO ESP32 {profile}",
                "version": identity.version,
                "bom-ref": f"pkg:generic/ulsa-evo-esp32-{profile}@{identity.version}",
                "properties": [
                    {"name": "ulsa:profile", "value": profile},
                    {"name": "ulsa:versionCode", "value": str(identity.version_code)},
                    {"name": "ulsa:revision", "value": str(identity.revision)},
                    {"name": "ulsa:releaseTag", "value": identity.release_tag},
                ],
            }
        },
        "components": [
            {
                "type": "library" if component["scope"] == "runtime" else "application",
                "name": component["name"],
                "version": component["version"],
                "purl": component["purl"],
                "licenses": [{"license": _cyclonedx_license(component["license"])}],
                "externalReferences": [{"type": "vcs", "url": component["source"]}],
                "properties": [
                    {"name": "ulsa:scope", "value": component["scope"]},
                    {"name": "ulsa:packageVersion", "value": component["packageVersion"]},
                ],
            }
            for component in selected
        ],
    }


def _package_roots() -> list[Path]:
    roots = [ROOT / ".pio" / "libdeps"]
    configured = os.environ.get("PLATFORMIO_CORE_DIR")
    if configured:
        roots.append(Path(configured))
    roots.append(Path.home() / ".platformio")
    return roots


def _local_candidates(item: dict[str, Any]) -> list[Path]:
    candidates: list[Path] = []
    cache_root = os.environ.get("ULSA_LICENSE_CACHE")
    if cache_root:
        candidates.append(Path(cache_root) / item["cacheName"])
    for pattern in item.get("localCandidates", []):
        candidates.extend(ROOT.glob(pattern))

    component = item["component"]
    patterns: list[str] = []
    if component == "Arduino-ESP32":
        patterns = ["packages/framework-arduinoespressif32/LICENSE.md"]
    elif component == "PlatformIO Espressif 32":
        patterns = ["platforms/espressif32/LICENSE"]
    elif component == "esptool":
        patterns = ["packages/tool-esptoolpy@*/LICENSE", "packages/tool-esptoolpy/LICENSE"]
    elif component == "GCC Toolchain for ESP32 RISC-V":
        patterns = ["packages/toolchain-riscv32-esp/LICENSE"]
    for root in _package_roots():
        for pattern in patterns:
            candidates.extend(root.glob(pattern))
    return [path for path in candidates if path.is_file()]


def _retrieve_license_file(item: dict[str, Any]) -> tuple[bytes, str]:
    mismatches: list[str] = []
    for candidate in _local_candidates(item):
        try:
            payload = candidate.read_bytes()
        except OSError:
            continue
        if sha256_bytes(payload) == item["sha256"]:
            return payload, "local-package-cache"
        mismatches.append(str(candidate))
    try:
        request = Request(item["url"], headers={"User-Agent": "ULSA-EVO-license-verifier/1"})
        with urlopen(request, timeout=30) as response:
            payload = response.read()
    except (OSError, URLError) as exc:
        detail = f"; local candidates mismatched: {', '.join(mismatches)}" if mismatches else ""
        raise RuntimeError(f"unable to retrieve {item['id']} from its immutable source{detail}: {exc}") from exc
    actual = sha256_bytes(payload)
    if actual != item["sha256"]:
        raise RuntimeError(
            f"license source hash mismatch for {item['id']}: expected {item['sha256']}, got {actual}"
        )
    return payload, item["url"]


def _map_archives(profile: str, contract: dict[str, Any], *, require: bool) -> list[dict[str, Any]]:
    env_name = PROFILE_ENVIRONMENTS[profile]
    map_path = ROOT / ".pio" / "build" / env_name / "firmware.map"
    if not map_path.is_file():
        if require:
            raise RuntimeError(f"linker map is missing for {profile}: {map_path}")
        return []
    payload = map_path.read_text(encoding="utf-8", errors="replace")
    archive_names: set[str] = set()
    archive_pattern = re.compile(r"(?P<archive>lib[^/\s()]+\.a)\(")
    for line in payload.splitlines():
        archive_names.update(
            match.group("archive") for match in archive_pattern.finditer(line)
        )
    names = set(archive_names)
    names.update(
        name for name in contract["archiveComponents"]["overrides"]
        if name in payload
    )
    if not names and require:
        raise RuntimeError(f"linker map has no archives for {profile}: {map_path}")
    result: list[dict[str, Any]] = []
    for name in sorted(names):
        component = contract["archiveComponents"]["overrides"].get(
            name, contract["archiveComponents"]["default"]
        )
        archive_license_ids = contract["archiveComponents"].get("archiveLicenses", {}).get(name, [])
        if not archive_license_ids and component == contract["archiveComponents"]["default"]:
            archive_license_ids = ["esp-idf-license", "esp-idf-copyright"]
        archive_paths = list((ROOT / ".pio" / "build" / env_name).rglob(name))
        if not archive_paths:
            for package_root in _package_roots():
                archive_paths.extend(package_root.rglob(name))
        if not archive_paths:
            if require:
                raise RuntimeError(f"linked archive {name} is not present for {profile}")
            result.append({
                "archive": name,
                "component": component,
                "licenseFileIds": archive_license_ids,
                "resolved": False,
            })
            continue
        archive = sorted(archive_paths)[0]
        result.append({
            "archive": name,
            "component": component,
            "licenseFileIds": archive_license_ids,
            "size": archive.stat().st_size,
            "sha256": sha256(archive),
            "resolved": True,
        })
    return result


def _metadata_candidates(component: str) -> list[Path]:
    candidates: list[Path] = []
    for root in _package_roots():
        for pattern in PACKAGE_METADATA_PATTERNS.get(component, ()):
            if root.name == "libdeps" and pattern.startswith("libdeps/"):
                pattern = pattern[len("libdeps/"):]
            candidates.extend(root.glob(pattern))
    return sorted({path for path in candidates if path.is_file()})


def _metadata_version(path: Path) -> str | None:
    try:
        # ``Path.suffix`` is empty for the dotfile name ``.piopm``.
        if path.suffix == ".json" or path.name == ".piopm":
            value = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(value, dict) and isinstance(value.get("version"), str):
                return value["version"]
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("version="):
                return line.split("=", 1)[1].strip()
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    return None


def _package_evidence(components: list[dict], license_files: list[dict[str, Any]], *, require: bool) -> dict[str, Any]:
    evidence: list[dict[str, Any]] = []
    for component in components:
        metadata_hashes = []
        for path in _metadata_candidates(component["name"]):
            metadata_hashes.append({
                "sha256": sha256(path),
                "bytes": path.stat().st_size,
                "version": _metadata_version(path),
            })
        bound = [item["id"] for item in license_files if item["component"] == component["name"]]
        if not bound:
            bound = [
                item["id"] for item in license_files
                if item["license"] == component["license"]
                and item["kind"] == "license"
            ]
        evidence.append({
            "name": component["name"],
            "packageVersion": component["packageVersion"],
            "licenseFileIds": sorted(bound),
            "metadata": metadata_hashes,
            "metadataResolved": bool(metadata_hashes),
        })
        if (
            require
            and component["scope"] == "runtime"
            and component["name"] not in {"IANA Time Zone Database", "ESP-IDF bundled SDK"}
            and not metadata_hashes
        ):
            raise RuntimeError(
                f"PlatformIO package metadata is missing for runtime component: {component['name']}"
            )
        observed_versions = {item["version"] for item in metadata_hashes if item["version"]}
        expected_version = component["packageVersion"]
        if component["name"] == "PlatformIO Espressif 32":
            expected_version = component["version"]
        if require and component["name"] in {"PlatformIO Espressif 32", "Arduino-ESP32"}:
            if expected_version not in observed_versions:
                raise RuntimeError(
                    f"resolved package version does not match the component contract: {component['name']}"
                )
    platformio = next((item for item in components if item["name"] == "PlatformIO Core"), None)
    if platformio:
        observed = None
        executable = shutil.which("pio")
        if executable:
            result = subprocess.run([executable, "--version"], capture_output=True, text=True, check=False)
            observed = result.stdout.strip() or result.stderr.strip()
        platformio_license_ids = sorted(
            item["id"] for item in license_files if item["component"] == "PlatformIO Core"
        )
        evidence.append({
            "name": "PlatformIO Core executable",
            "expected": platformio["packageVersion"],
            "observed": observed,
            "matched": bool(observed and f"version {platformio['packageVersion']}" in observed),
            "licenseFileIds": platformio_license_ids,
        })
        if require and not evidence[-1]["matched"]:
            raise RuntimeError("resolved PlatformIO Core version does not match the component contract")
    return {"components": evidence}


def _write_deterministic_tar(source_dir: Path, target: Path) -> None:
    with target.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for path in sorted(source_dir.rglob("*")):
                    if not path.is_file():
                        continue
                    arcname = path.relative_to(source_dir.parent).as_posix()
                    info = archive.gettarinfo(str(path), arcname=arcname)
                    info.mtime = 0
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mode = 0o644
                    with path.open("rb") as handle:
                        archive.addfile(info, handle)


def generate_license_bundle(
    output_dir: Path,
    components: list[dict],
    *,
    require_resolved: bool = False,
) -> tuple[Path, Path]:
    contract = load_license_contract()
    license_dir = output_dir / "LICENSES"
    if license_dir.exists():
        if license_dir.is_symlink():
            raise RuntimeError("license bundle output directory must not be a symlink")
        # LICENSES is generated output. Refresh it so an old notice cannot
        # survive into a new Release tarball or manifest.
        shutil.rmtree(license_dir)
    license_dir.mkdir(parents=True, exist_ok=True)
    resolved_files: list[dict[str, Any]] = []
    for item in contract["licenseFiles"]:
        payload, retrieval = _retrieve_license_file(item)
        target = output_dir / item["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        resolved_files.append({
            "id": item["id"],
            "component": item["component"],
            "license": item["license"],
            "kind": item["kind"],
            "path": item["path"],
            "source": item["url"],
            "sha256": sha256_bytes(payload),
            "bytes": len(payload),
            "retrievedFrom": retrieval,
        })

    notice = license_dir / "NOTICE"
    notice.write_text(
        "ULSA EVO ESP32 firmware third-party license and notice bundle.\n\n"
        + "\n".join(
            f"- {item['component']}: {item['path']} ({item['license']})"
            for item in resolved_files
        )
        + "\n\nSee docs/OPEN_SOURCE_LICENSES.md for corresponding-source and relink instructions.\n",
        encoding="utf-8",
    )
    linked_archives = {
        profile: _map_archives(profile, contract, require=require_resolved)
        for profile in PROFILES
    }
    package_evidence = _package_evidence(components, resolved_files, require=require_resolved)
    sbom_hashes = {}
    for profile in PROFILES:
        sbom = output_dir / f"ulsa-evo-esp32-{profile}-sbom.cdx.json"
        if not sbom.is_file():
            if require_resolved:
                raise RuntimeError(f"SBOM is missing for {profile}")
            continue
        sbom_hashes[profile] = sha256(sbom)
    manifest = {
        "schemaVersion": 1,
        "componentContractSha256": component_contract_sha256(),
        "noticeSha256": sha256(NOTICE_PATH),
        "projectLicenseSha256": sha256(ROOT / "LICENSE"),
        "sbomSha256": sbom_hashes,
        "licenseFiles": resolved_files,
        "bundleNoticeSha256": sha256(notice),
        "linkedArchives": linked_archives,
        "packageEvidence": package_evidence,
        "resolutionStatus": "resolved" if require_resolved else "best-effort",
        "sourceOffers": contract["sourceOffers"],
        "legalReviewStatus": contract["legalReview"]["status"],
        "manifestPath": "LICENSES/license-bundle-manifest.json",
    }
    root_manifest = output_dir / "license-bundle-manifest.json"
    root_manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    shutil.copy2(root_manifest, license_dir / "license-bundle-manifest.json")
    tar_path = output_dir / "ulsa-evo-esp32-license-bundle.tar.gz"
    _write_deterministic_tar(license_dir, tar_path)
    return root_manifest, tar_path


def verify_compliance_output(output_dir: Path, *, require_resolved: bool = False) -> None:
    components = load_components()
    expected_notice = render_notice(components)
    output_notice = output_dir / "THIRD_PARTY_NOTICES.md"
    if not output_notice.is_file() or output_notice.read_text(encoding="utf-8") != expected_notice:
        raise RuntimeError("generated third-party notice is not bound to the component contract")
    bundle_manifest = output_dir / "license-bundle-manifest.json"
    bundle_archive = output_dir / "ulsa-evo-esp32-license-bundle.tar.gz"
    if not bundle_manifest.is_file() or not bundle_archive.is_file():
        raise RuntimeError("license bundle assets are missing")
    manifest = json.loads(bundle_manifest.read_text(encoding="utf-8"))
    if manifest.get("componentContractSha256") != component_contract_sha256():
        raise RuntimeError("license bundle component contract hash is stale")
    if manifest.get("noticeSha256") != sha256(output_notice):
        raise RuntimeError("license bundle notice hash is stale")
    if manifest.get("projectLicenseSha256") != sha256(ROOT / "LICENSE"):
        raise RuntimeError("license bundle project license hash is stale")
    for profile in PROFILES:
        sbom = output_dir / f"ulsa-evo-esp32-{profile}-sbom.cdx.json"
        if not sbom.is_file() or manifest.get("sbomSha256", {}).get(profile) != sha256(sbom):
            raise RuntimeError(f"license bundle SBOM hash is stale: {profile}")
    if require_resolved and manifest.get("resolutionStatus") != "resolved":
        raise RuntimeError("license bundle does not contain resolved linker evidence")
    for item in manifest.get("licenseFiles", []):
        path = output_dir / item["path"]
        if not path.is_file() or sha256(path) != item["sha256"]:
            raise RuntimeError(f"license bundle file hash mismatch: {item['path']}")
    known_license_ids = {item["id"] for item in manifest.get("licenseFiles", [])}
    package_evidence = manifest.get("packageEvidence", {}).get("components", [])
    if not isinstance(package_evidence, list):
        raise RuntimeError("license bundle package evidence is missing")
    for component in package_evidence:
        if not isinstance(component, dict) or not component.get("licenseFileIds"):
            raise RuntimeError("component has no license binding in the package evidence")
        if not set(component["licenseFileIds"]) <= known_license_ids:
            raise RuntimeError("package evidence references an unknown license file")
    linked = manifest.get("linkedArchives")
    if not isinstance(linked, dict):
        raise RuntimeError("license bundle linked archive evidence is missing")
    for profile in PROFILES:
        entries = linked.get(profile)
        if not isinstance(entries, list):
            raise RuntimeError(f"license bundle linked archive evidence is missing: {profile}")
        for entry in entries:
            if not isinstance(entry, dict) or not entry.get("licenseFileIds"):
                raise RuntimeError(f"linked archive has no license binding: {profile}")
            if not set(entry["licenseFileIds"]) <= known_license_ids:
                raise RuntimeError(f"linked archive references an unknown license file: {profile}")
            if require_resolved and entry.get("resolved") is not True:
                raise RuntimeError(f"linked archive is unresolved: {profile}")
    with tarfile.open(bundle_archive, "r:gz") as archive:
        members = {member.name for member in archive.getmembers() if member.isfile()}
        manifest_member = archive.extractfile("LICENSES/license-bundle-manifest.json")
        notice_member = archive.extractfile("LICENSES/NOTICE")
        if manifest_member is None or manifest_member.read() != bundle_manifest.read_bytes():
            raise RuntimeError("license bundle archive manifest does not match its root manifest")
        if notice_member is None or sha256_bytes(notice_member.read()) != manifest.get("bundleNoticeSha256"):
            raise RuntimeError("license bundle archive notice hash does not match its manifest")
    required_members = {item["path"] for item in manifest["licenseFiles"]}
    required_members |= {"LICENSES/NOTICE", "LICENSES/license-bundle-manifest.json"}
    if not required_members <= members:
        raise RuntimeError("license bundle archive is missing a generated file")


def generate_release_compliance(output_dir: Path, *, require_resolved: bool = False) -> list[Path]:
    components = load_components()
    output_dir.mkdir(parents=True, exist_ok=True)
    assets: list[Path] = []
    for profile in PROFILES:
        path = output_dir / f"ulsa-evo-esp32-{profile}-sbom.cdx.json"
        path.write_text(
            json.dumps(sbom_for_profile(profile, components), indent=2) + "\n",
            encoding="utf-8",
        )
        assets.append(path)
    notice = output_dir / "THIRD_PARTY_NOTICES.md"
    notice.write_text(render_notice(components), encoding="utf-8")
    assets.append(notice)
    license_path = output_dir / "LICENSE"
    shutil.copy2(ROOT / "LICENSE", license_path)
    assets.append(license_path)
    bundle_manifest, bundle_archive = generate_license_bundle(
        output_dir, components, require_resolved=require_resolved
    )
    assets.extend((bundle_manifest, bundle_archive))
    verify_compliance_output(output_dir, require_resolved=require_resolved)
    return assets


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--write-notice", action="store_true")
    parser.add_argument(
        "--require-resolved",
        action="store_true",
        help="require PlatformIO/package and both profile linker-map evidence",
    )
    parser.add_argument(
        "--require-legal-review",
        action="store_true",
        help="fail unless the contract contains an authorized legal approval",
    )
    args = parser.parse_args()
    components = load_components()
    if args.require_legal_review:
        assert_legal_review_approved()
    expected_notice = render_notice(components)
    if args.write_notice:
        NOTICE_PATH.write_text(expected_notice, encoding="utf-8")
    elif not NOTICE_PATH.is_file() or NOTICE_PATH.read_text(encoding="utf-8") != expected_notice:
        raise RuntimeError("THIRD_PARTY_NOTICES.md differs from the component contract")
    if args.output_dir:
        for path in generate_release_compliance(
            args.output_dir,
            require_resolved=args.require_resolved,
        ):
            print(path)
    print(f"Third-party compliance contract verified: {len(components)} components")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"Compliance artifact generation failed: {exc}")
        raise SystemExit(1)
