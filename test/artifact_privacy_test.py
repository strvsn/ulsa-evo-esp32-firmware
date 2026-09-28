#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from unittest.mock import DEFAULT


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from artifact_privacy import (  # noqa: E402
    PRIVATE_REPOSITORY_LOCATORS_ENV,
    assert_artifact_private_safe,
    artifact_privacy_findings,
    source_privacy_findings,
)
import verify_public_source as public_verifier  # noqa: E402


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

    def test_utf16_paths_in_binary_at_either_alignment_are_rejected(self) -> None:
        samples = (
            "/" + "Users/test-user/firmware.bin",
            "/" + "home/developer/firmware.bin",
            "D:" + "\\Users\\builder\\firmware.bin",
            "file:" + "//example/file.bin",
        )
        for encoding in ("utf-16-le", "utf-16-be"):
            for prefix in (b"\x00\xff", b"\x00\xff\x01"):
                for sample in samples:
                    with self.subTest(encoding=encoding, alignment=len(prefix) % 2, sample=sample):
                        payload = prefix + sample.encode(encoding) + b"\x00\x00\xff"
                        self.assertTrue(artifact_privacy_findings(payload))
                        self.assertTrue(source_privacy_findings(payload))

    def test_encoded_diagnostics_secrets_and_bom_are_rejected(self) -> None:
        samples = (
            "ULSA_" + "INTERNAL_DIAGNOSTICS",
            "-----BEGIN RSA " + "PRIVATE KEY-----",
            "gh" + "p_" + "A" * 40,
            "AK" + "IA" + "A" * 16,
        )
        for encoding in ("utf-16", "utf-16-le", "utf-16-be"):
            for sample in samples:
                with self.subTest(encoding=encoding, sample=sample[:16]):
                    self.assertTrue(artifact_privacy_findings(sample.encode(encoding)))

    def test_encoded_ci_exception_is_narrow(self) -> None:
        for encoding in ("utf-16-le", "utf-16-be"):
            allowed = "/" + "home/runner/work/firmware.elf"
            self.assertEqual(
                artifact_privacy_findings(allowed.encode(encoding), allow_ci_runner_paths=True), []
            )
            for suffix in ("-person", "2", ".local"):
                rejected = "/" + "home/runner" + suffix + "/firmware.elf"
                self.assertIn(
                    "Linux user-home path",
                    artifact_privacy_findings(rejected.encode(encoding), allow_ci_runner_paths=True),
                )

    def test_encoded_repository_locator_is_rejected(self) -> None:
        locator = "https://github.com/example/private-firmware"
        with patch.dict("os.environ", {PRIVATE_REPOSITORY_LOCATORS_ENV: locator}):
            for encoding in ("utf-16-le", "utf-16-be"):
                self.assertIn(
                    "private repository locator",
                    artifact_privacy_findings((locator.upper() + "/tree/main").encode(encoding)),
                )

    def test_source_only_prose_rule_does_not_reject_innocent_binary_words(self) -> None:
        marker = ("TO" + "DO").encode("utf-16-le")
        self.assertEqual(artifact_privacy_findings(marker), [])
        self.assertIn("release task marker", source_privacy_findings(marker))
        self.assertEqual(source_privacy_findings(b"while processing a child target"), [])

    def test_findings_are_unique_and_encoded_file_errors_are_redacted(self) -> None:
        sample = ("/" + "Users/private-person/firmware.bin").encode("utf-16-be")
        self.assertEqual(artifact_privacy_findings(sample + sample), ["macOS user-home path"])
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "firmware.bin"
            artifact.write_bytes(sample)
            with self.assertRaises(RuntimeError) as raised:
                assert_artifact_private_safe(artifact)
            self.assertIn("macOS user-home path", str(raised.exception))
            self.assertNotIn("private-person", str(raised.exception))

    def test_public_tree_gate_rejects_encoded_content_without_printing_it(self) -> None:
        for encoding in ("utf-16-le", "utf-16-be"):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "probe.cpp").write_bytes(
                    b"\xff" + ("/" + "Users/private-person/source.cpp").encode(encoding)
                )
                with patch.multiple(public_verifier, ROOT=root, REQUIRED_PATHS=(), tracked_files=DEFAULT) as mocks:
                    mocks["tracked_files"].return_value = ["probe.cpp"]
                    with self.assertRaisesRegex(RuntimeError, "macOS user-home path") as raised:
                        public_verifier.verify_tree()
                self.assertNotIn("private-person", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
