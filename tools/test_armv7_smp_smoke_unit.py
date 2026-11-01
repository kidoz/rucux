#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Evidence gates must reject incomplete or faulting ARMv7 guests."""

import unittest

from test_armv7_smp_smoke import accepted, required_markers


class ArmSmpEvidenceTest(unittest.TestCase):
    def test_complete_single_and_four_cpu_runs_pass(self) -> None:
        for cpus in (1, 4):
            self.assertTrue(accepted("\n".join(required_markers(cpus)), cpus, 0))

    def test_every_marker_is_required(self) -> None:
        for cpus in (1, 4):
            text = "\n".join(required_markers(cpus))
            for marker in required_markers(cpus):
                with self.subTest(cpus=cpus, marker=marker):
                    self.assertFalse(accepted(text.replace(marker, ""), cpus, 0))

    def test_faults_dominate_success_markers(self) -> None:
        text = "\n".join(required_markers(4))
        for fault in (
            "Data Abort",
            "Prefetch Abort",
            "Undefined Instruction",
            "ARMv7 TLB mismatch",
            "ARMv7 scheduler: FAIL",
            "PANIC",
        ):
            self.assertFalse(accepted(text + fault, 4, 0))

    def test_one_cpu_skip_cannot_pass_remote_gate(self) -> None:
        self.assertFalse(accepted("\n".join(required_markers(1)), 4, 0))

    def test_running_or_failed_process_is_rejected(self) -> None:
        for status in (None, 1, -11, 124):
            self.assertFalse(accepted("\n".join(required_markers(4)), 4, status))

    def test_boot_count_cannot_replace_individual_timer_or_preemption_evidence(self) -> None:
        text = "\n".join(required_markers(4))
        for marker in ("CPU2: timer IRQ verified", "ARMv7 scheduler: timer preemption PASS (8 workers)"):
            self.assertFalse(accepted(text.replace(marker, ""), 4, 0))


if __name__ == "__main__":
    unittest.main()
