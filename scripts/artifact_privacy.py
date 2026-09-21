#!/usr/bin/env python3
"""Fail-closed privacy checks for firmware images and release assets."""

from __future__ import annotations

import json
import os
import re
from pathlib import Path


FORBIDDEN_SOURCE_CONTENT = (
    ("internal diagnostics", b"ULSA_" + b"INTERNAL_DIAGNOSTICS"),
    ("private DPS register marker", b"REG_" + b"PRIVATE_" + b"DPS"),
    ("private debug-pulse marker", b"REG_" + b"PRIVATE_DEBUG_PULSE_INDEX"),
    ("private identity marker", b"UUID_CHAR_" + b"PRIVATE_"),
    ("private DPS snapshot marker", b"UlsaEvoI2c" + b"DpsSnapshot"),
    ("private BLE diagnostics marker", b"BlePrivate" + b"DpsDebugStats"),
    ("raw time-of-flight marker", b"raw_" + b"tof_"),
    ("private DPS BLE reset marker", b"reset-" + b"dps-ble"),
    ("manufacturing-only marker", b"Manu" + b"facturing"),
    ("release-readiness marker", b"Release " + b"Readiness"),
    ("release decision marker", b"NO" + b"-GO"),
    ("release task marker", b"TO" + b"DO"),
    ("hardware-in-the-loop marker", b"H" + b"IL"),
    ("macOS user-home path", b"/" + b"Users/"),
    ("Linux user-home path", b"/" + b"home/"),
    ("Windows user-home path", b":" + b"\\Users\\"),
    ("local file URL", b"file:" + b"//"),
)

FORBIDDEN_SOURCE_MARKERS = tuple(marker for _, marker in FORBIDDEN_SOURCE_CONTENT)

# Broad prose tokens stay source-only; compiled code can contain them innocently.
FORBIDDEN_ARTIFACT_CONTENT = (
    *FORBIDDEN_SOURCE_CONTENT[:8],
    *FORBIDDEN_SOURCE_CONTENT[-4:],
)
CI_RUNNER_PATH_PREFIX = b"/" + b"home/runner"
PRIVATE_REPOSITORY_LOCATORS_ENV = "ULSA_ARTIFACT_PRIVATE_LOCATORS"

SECRET_PATTERNS = (
    ("private-key PEM header", re.compile(rb"-----BEGIN (?:RSA |EC |DSA |OPENSSH )?PRIVATE KEY-----")),
    ("GitHub access token", re.compile(rb"gh[pousr]_[A-Za-z0-9]{30,}")),
    ("GitHub fine-grained token", re.compile(rb"github_pat_[A-Za-z0-9_]{40,}")),
    ("AWS access-key ID", re.compile(rb"AKIA[0-9A-Z]{16}")),
    ("Google API key", re.compile(rb"AIza[0-9A-Za-z_-]{35}")),
    ("Slack access token", re.compile(rb"xox[baprs]-[A-Za-z0-9-]{20,}")),
)


def private_repository_markers() -> tuple[bytes, ...]:
    """Load private repository locators without embedding them in public source."""
    values = [
        value.strip()
        for value in os.environ.get(PRIVATE_REPOSITORY_LOCATORS_ENV, "").splitlines()
        if value.strip()
    ]
    policy_path = Path(__file__).resolve().parents[1] / "config" / "public_source_policy.json"
    if policy_path.is_file():
        try:
            policy = json.loads(policy_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise RuntimeError("Could not read private source policy for artifact privacy checks") from error
        forbidden_content = policy.get("forbiddenContent", [])
        if not isinstance(forbidden_content, list):
            raise RuntimeError("Private source policy forbiddenContent must be a list")
        values.extend(
            value.strip()
            for value in forbidden_content
            if isinstance(value, str) and "github.com/" in value.lower()
        )

    markers: list[bytes] = []
    for value in values:
        marker = re.sub(r"^(?:https?|ssh)://", "", value.lower())
        marker = re.sub(r"^git@([^:]+):", r"\1/", marker)
        marker = marker.removesuffix(".git").rstrip("/")
        if marker:
            markers.append(marker.encode("utf-8"))
    return tuple(dict.fromkeys(markers))


def artifact_privacy_findings(
    payload: bytes,
    *,
    allow_ci_runner_paths: bool = False,
) -> list[str]:
    """Return privacy-marker and high-confidence credential findings."""
    lowered = payload.lower()
    if allow_ci_runner_paths:
        runner_root = re.escape(CI_RUNNER_PATH_PREFIX)
        lowered = re.sub(
            runner_root + rb"(?=[/\\\x00 \t\r\n\"']|$)",
            b"<ci-runner>",
            lowered,
        )
    findings = [
        label for label, marker in FORBIDDEN_ARTIFACT_CONTENT if marker.lower() in lowered
    ]
    if any(marker in lowered for marker in private_repository_markers()):
        findings.append("private repository locator")
    findings.extend(label for label, pattern in SECRET_PATTERNS if pattern.search(payload))
    return findings


def assert_artifact_private_safe(
    path: Path,
    *,
    allow_ci_runner_paths: bool = False,
) -> None:
    findings = artifact_privacy_findings(
        path.read_bytes(),
        allow_ci_runner_paths=allow_ci_runner_paths,
    )
    if findings:
        raise RuntimeError(
            f"Artifact privacy check failed for {path.name}: " + ", ".join(findings)
        )
