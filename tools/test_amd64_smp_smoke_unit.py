#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reject incomplete, mismatched or faulting amd64 SMP evidence."""

import unittest

from test_amd64_smp_smoke import accepted, required_markers


class Amd64SmpEvidenceTest(unittest.TestCase):
    def test_complete_single_and_four_cpu_logs(self) -> None:
        for cpus in (1, 4):
            self.assertTrue(accepted("\n".join(required_markers(cpus)), cpus, 0))

    def test_every_required_marker_is_mandatory(self) -> None:
        for cpus in (1, 4):
            text = "\n".join(required_markers(cpus))
            for marker in required_markers(cpus):
                with self.subTest(cpus=cpus, missing=marker):
                    self.assertFalse(accepted(text.replace(marker, ""), cpus, 0))

    def test_single_cpu_skips_cannot_satisfy_four_cpu_gate(self) -> None:
        self.assertFalse(accepted("\n".join(required_markers(1)), 4, 0))

    def test_failure_after_complete_success_is_rejected(self) -> None:
        text = "\n".join(required_markers(4))
        for failure in (
            "SMP retirement: check 200",
            "TLB: remote IRQ-masked replacement FAIL",
            "#PF at 0x8000",
            "Fault RIP=0x0",
            "EXCEPTION 13",
            "PANIC",
        ):
            with self.subTest(failure=failure):
                self.assertFalse(accepted(text + failure, 4, 0))

    def test_aggregate_boot_count_cannot_replace_per_ap_timer_evidence(self) -> None:
        text = "\n".join(required_markers(4)).replace("AP2: timer IRQ verified", "")
        self.assertFalse(accepted(text, 4, 0))

    def test_running_or_abnormally_exited_guest_is_not_success(self) -> None:
        text = "\n".join(required_markers(4))
        for code in (None, 1, 124, -11):
            self.assertFalse(accepted(text, 4, code))


if __name__ == "__main__":
    unittest.main()
