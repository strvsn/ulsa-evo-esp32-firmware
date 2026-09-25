#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from artifact_privacy import (  # noqa: E402
    PRIVATE_REPOSITORY_LOCATORS_ENV,
    assert_artifact_private_safe,
    artifact_privacy_findings,
)


class ArtifactPrivacyTest(unittest.TestCase):
    def test_clean_payload_passes(self) -> None:
        self.assertEqual(artifact_privacy_findings(b"firmware image with child target"), [])

    def test_host_paths_and_file_urls_are_rejected(self) -> None:
        samples = (
            b"/" + b"Users/test-user/Developer/firmware.bin",
            b"/" + b"home/runner/work/firmware.bin",
            b"D:" + b"\\Users\\builder\\firmware.bin",
            b"file:" + b"//" + b"/" + b"Users/test-user/firmware.bin",
        )
        for sample in samples:
            with self.subTest(sample=sample):
                self.assertTrue(artifact_privacy_findings(sample))

    def test_debug_elf_mode_allows_only_the_shared_ci_runner_root(self) -> None:
        runner_path = b"/" + b"home/runner/work/project/firmware.elf"
        self.assertIn("Linux user-home path", artifact_privacy_findings(runner_path))
        self.assertEqual(
            artifact_privacy_findings(runner_path, allow_ci_runner_paths=True),
            [],
        )
        developer_path = b"/" + b"home/developer/work/project/firmware.elf"
        self.assertIn(
            "Linux user-home path",
            artifact_privacy_findings(developer_path, allow_ci_runner_paths=True),
        )

    def test_private_content_and_high_confidence_secrets_are_rejected(self) -> None:
        samples = (
            b"ULSA_" + b"INTERNAL_DIAGNOSTICS",
            b"-----BEGIN RSA " + b"PRIVATE KEY-----",
            ("gh" + "p_" + "A" * 40).encode("ascii"),
            ("AK" + "IA" + "A" * 16).encode("ascii"),
        )
        for sample in samples:
            with self.subTest(sample=sample[:24]):
                self.assertTrue(artifact_privacy_findings(sample))

    def test_configured_private_repository_locator_is_rejected_without_embedding_it(self) -> None:
        locator = "https://github.com/example/private-firmware"
        with patch.dict("os.environ", {PRIVATE_REPOSITORY_LOCATORS_ENV: locator}):
            findings = artifact_privacy_findings(
                f"firmware metadata: {locator}/tree/main".encode("ascii")
            )
        self.assertIn("private repository locator", findings)

    def test_file_gate_fails_closed_without_printing_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "firmware.bin"
            artifact.write_bytes(b"prefix /" + b"Users/private-person/firmware.bin")
            with self.assertRaisesRegex(RuntimeError, "macOS user-home path"):
                assert_artifact_private_safe(artifact)


if __name__ == "__main__":
    unittest.main()
