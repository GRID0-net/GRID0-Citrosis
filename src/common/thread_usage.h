// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "common/common_types.h"

/**
 * Per-thread host CPU usage (Citrosis).
 *
 * Emulator threads register themselves for their lifetime. The performance log samples the
 * CPU time each one consumed since the previous sample, read from the OS thread CPU clock,
 * so nothing is measured on the hot paths. A thread close to 100% is the frame-rate limiter:
 * GPU (command processing), VulkanWorker (Vulkan recording/submission) or a CPUCore (guest code).
 * If no thread is saturated while the game misses its target, the host GPU or a wait is the cause.
 */
namespace Common::ThreadUsage {

/// Registers the calling thread under @p name until destruction. Create it on the thread itself.
class Registration {
public:
    explicit Registration(std::string name);
    ~Registration();

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;

private:
    u64 id;
};

/// Returns "name=NN% ..." for registered threads since the previous call, or an empty string when
/// the platform has no per-thread CPU clock.
std::string SampleReport();

} // namespace Common::ThreadUsage
