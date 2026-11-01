// SPDX-License-Identifier: MIT
#pragma once
namespace kernel::scheduler {
// Smoke product only: real CPU handoff, retirement, IPC, signals and waitpid.
bool verify_smp_retirement() noexcept;
} // namespace kernel::scheduler
