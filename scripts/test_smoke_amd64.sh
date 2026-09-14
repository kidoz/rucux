#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

POWER_LOG="${RUCUX_SMOKE_AMD64_POWER_LOG:-smoke_poweroff_run.log}"
REBOOT_BOOT1_LOG="${RUCUX_SMOKE_AMD64_REBOOT_BOOT1_LOG:-smoke_reboot_boot1.log}"
REBOOT_BOOT2_LOG="${RUCUX_SMOKE_AMD64_REBOOT_BOOT2_LOG:-smoke_reboot_boot2.log}"
REBOOT_JOURNAL_LOG="${RUCUX_SMOKE_AMD64_REBOOT_JOURNAL_LOG:-smoke_reboot_journal.log}"

scripts/test_smoke_poweroff.sh
scripts/test_smoke_reboot.sh

echo "smoke-amd64: PASS"
echo "smoke-amd64: logs captured in ${POWER_LOG}, ${REBOOT_BOOT1_LOG}, ${REBOOT_BOOT2_LOG}, and ${REBOOT_JOURNAL_LOG}"

