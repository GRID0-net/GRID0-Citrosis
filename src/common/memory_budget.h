// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "common/common_types.h"

/**
 * Host memory budget (Citrosis).
 *
 * On desktops the GPU caches size themselves from the Vulkan heap, which is fine when the GPU
 * has its own memory. On phones the "device local" heap is the whole system RAM, so the caches
 * would happily grow to several GiB and Android's low memory killer ends the process. A frontend
 * that knows the real RAM size (Android) calls SetPhysicalMemory() before the renderer starts;
 * the texture and buffer caches then cap their garbage collection thresholds with GetLimits().
 *
 * The frontend also reports memory pressure (from onTrimMemory and /proc/meminfo), which makes
 * the caches collect harder until the pressure goes away.
 */
namespace Common::MemoryBudget {

struct Limits {
    u64 texture_expected; ///< Texture cache starts high-priority collection above this.
    u64 texture_critical; ///< Texture cache collects aggressively above this.
    u64 buffer_expected;  ///< Buffer cache starts collecting above this.
    u64 buffer_critical;  ///< Buffer cache collects aggressively above this.
    u64 staging_expected; ///< Staging upload backpressure starts above this (including ring).
};

enum class Pressure : u8 {
    None = 0,
    Moderate = 1, ///< The system is getting low: collect everything that is cheap to rebuild.
    Critical = 2, ///< The process is next in line to be killed: collect aggressively.
};

/// Sets the total physical RAM in bytes. 0 (the default) means no budget: desktop behaviour.
void SetPhysicalMemory(u64 total_bytes);

/// Total physical RAM given to SetPhysicalMemory(), or 0.
u64 GetPhysicalMemory();
void SetAvailableMemory(u64 available_bytes);
u64 GetAvailableMemory();

/// Live GC targets scaled from physical RAM and pressure, or nullopt without a budget.
std::optional<Limits> GetLimits();

void SetPressure(Pressure pressure);
Pressure GetPressure();

} // namespace Common::MemoryBudget
