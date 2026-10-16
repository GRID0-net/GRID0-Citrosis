// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include "common/settings.h"

namespace VideoCommon::AndroidPerformance {
inline bool Enabled() {
#ifdef ANDROID
    return Settings::values.android_vulkan_performance.GetValue();
#else
    return false;
#endif
}

enum class Work : size_t {
    Translation,
    ShaderBuild,
    ShaderWait,
    UploadWait,
    TextureGc,
    BufferGc,
    Draw,
    TextureUpload,
    UploadRead,
    UploadUnswizzle,
    UploadConvert,
    UploadSanitize,
    UploadRecord,
    UploadFusedCopy,
    Count
};
struct Counter {
    std::atomic<u64> nanoseconds{0};
    std::atomic<u64> calls{0};
};
inline std::array<Counter, static_cast<size_t>(Work::Count)> counters{};
class Scope {
public:
    explicit Scope(Work work_) : work{work_}, enabled{Enabled()} {
        if (enabled)
            start = std::chrono::steady_clock::now();
    }
    ~Scope() {
        Stop();
    }
    void Stop() {
        if (!enabled)
            return;
        auto& counter = counters[static_cast<size_t>(work)];
        counter.nanoseconds.fetch_add(
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count()),
            std::memory_order_relaxed);
        counter.calls.fetch_add(1, std::memory_order_relaxed);
        enabled = false;
    }

private:
    Work work;
    bool enabled;
    std::chrono::steady_clock::time_point start{};
};
} // namespace VideoCommon::AndroidPerformance
