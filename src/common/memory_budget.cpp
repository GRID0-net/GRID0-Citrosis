// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>

#include "common/literals.h"
#include "common/memory_budget.h"

namespace Common::MemoryBudget {

using namespace Common::Literals;

namespace {
std::atomic<u64> g_physical_memory{0};
std::atomic<u64> g_available_memory{0};
std::atomic<u8> g_pressure{0};

constexpr u64 Scale(u64 total, u64 per_mille, u64 low, u64 high) {
    return std::clamp<u64>(total / 1000 * per_mille, low, high);
}
} // namespace

void SetPhysicalMemory(u64 total_bytes) {
    g_physical_memory.store(total_bytes, std::memory_order_relaxed);
}

u64 GetPhysicalMemory() {
    return g_physical_memory.load(std::memory_order_relaxed);
}

void SetAvailableMemory(u64 available_bytes) {
    g_available_memory.store(available_bytes, std::memory_order_relaxed);
}

u64 GetAvailableMemory() {
    return g_available_memory.load(std::memory_order_relaxed);
}

std::optional<Limits> GetLimits() {
    const u64 total = GetPhysicalMemory();
    if (total == 0) {
        return std::nullopt;
    }
    // The 7233 MiB device was killed again at 4817 MiB RSS after the first cache cuts.
    // Reserve more headroom for guest RAM, staging, driver allocations and Android.
    // These are GC targets, not allocation caps; upload backpressure handles staging bursts.
    Limits limits{
        .texture_expected = Scale(total, 40, 256_MiB, 1536_MiB),
        .texture_critical = Scale(total, 60, 384_MiB, 2304_MiB),
        .buffer_expected = Scale(total, 14, 96_MiB, 384_MiB),
        .buffer_critical = Scale(total, 24, 160_MiB, 640_MiB),
        .staging_expected = Scale(total, 12, 64_MiB, 256_MiB),
    };
    const auto pressure = GetPressure();
    const u64 numerator = pressure == Pressure::Critical   ? 2
                          : pressure == Pressure::Moderate ? 3
                                                           : 4;
    limits.texture_expected = limits.texture_expected / 4 * numerator;
    limits.texture_critical = limits.texture_critical / 4 * numerator;
    limits.buffer_expected = limits.buffer_expected / 4 * numerator;
    limits.buffer_critical = limits.buffer_critical / 4 * numerator;
    limits.staging_expected = limits.staging_expected / 4 * numerator;
    return limits;
}

void SetPressure(Pressure pressure) {
    g_pressure.store(static_cast<u8>(pressure), std::memory_order_relaxed);
}

Pressure GetPressure() {
    return static_cast<Pressure>(g_pressure.load(std::memory_order_relaxed));
}

} // namespace Common::MemoryBudget
