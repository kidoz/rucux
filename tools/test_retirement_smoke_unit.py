#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Evidence policy tests: incomplete or failed guests must never pass a campaign."""

import unittest
from pathlib import Path

from test_retirement_smoke import accepted, required_markers, workspace_build


class RetirementEvidenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.text = "\n".join(required_markers(37, 768))

    def test_complete_matching_guest_passes(self) -> None:
        self.assertTrue(accepted(self.text, 37, 768, 0))

    def test_each_required_marker_is_mandatory(self) -> None:
        for marker in required_markers(37, 768):
            with self.subTest(marker=marker):
                self.assertFalse(accepted(self.text.replace(marker, ""), 37, 768, 0))

    def test_failure_dominates_complete_success_output(self) -> None:
        for failure in (
            "SMP retirement: check 215",
            "SMP retirement: FAIL",
            "shared mm: FAIL",
            "shared mm: root teardown FAIL",
            "*** EL0 data abort",
            "PANIC",
        ):
            with self.subTest(failure=failure):
                self.assertFalse(accepted(self.text + "\n" + failure, 37, 768, 0))

    def test_wrong_seed_or_round_count_cannot_be_replayed_as_success(self) -> None:
        self.assertFalse(accepted(self.text, 38, 768, 0))
        self.assertFalse(accepted(self.text, 37, 48, 0))

    def test_crash_or_running_process_is_not_a_pass(self) -> None:
        for status in (-11, 1, 124, None):
            with self.subTest(status=status):
                self.assertFalse(accepted(self.text, 37, 768, status))

    def test_artifacts_cannot_escape_workspace_build_directory(self) -> None:
        with self.assertRaises(ValueError):
            workspace_build(Path("../retirement"))
        self.assertEqual(workspace_build(Path("build/retirement-test")).name, "retirement-test")


if __name__ == "__main__":
    unittest.main()
