// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

#include <fmt/format.h>

#include "common/gpu_frame_stats.h"

namespace Common::GpuFrameStats {

namespace {

constexpr size_t ROLE_COUNT = static_cast<size_t>(Role::Count);

thread_local Role current_role = Role::Other;

std::atomic<u64> host_gpu_ns{0};
std::atomic<u64> host_gpu_samples{0};
std::array<std::atomic<u64>, ROLE_COUNT> wait_ns{};
std::array<std::atomic<u64>, ROLE_COUNT> wait_count{};
std::atomic<u64> present_frame_wait_ns{0};
std::atomic<u64> submits{0};
std::atomic<u64> early_submits{0};
std::atomic<u64> drains{0};

std::mutex sample_mutex;
std::chrono::steady_clock::time_point last_sample{};

double Ms(u64 ns) {
    return static_cast<double>(ns) / 1'000'000.0;
}

} // Anonymous namespace

void SetThreadRole(Role role) {
    current_role = role;
}

void AddHostGpuTime(u64 ns) {
    host_gpu_ns.fetch_add(ns, std::memory_order_relaxed);
    host_gpu_samples.fetch_add(1, std::memory_order_relaxed);
}

void AddHostWait(u64 ns) {
    const auto index = static_cast<size_t>(current_role);
    wait_ns[index].fetch_add(ns, std::memory_order_relaxed);
    wait_count[index].fetch_add(1, std::memory_order_relaxed);
}

void AddPresentFrameWait(u64 ns) {
    present_frame_wait_ns.fetch_add(ns, std::memory_order_relaxed);
}

void AddSubmit(bool early) {
    submits.fetch_add(1, std::memory_order_relaxed);
    if (early) {
        early_submits.fetch_add(1, std::memory_order_relaxed);
    }
}

void AddDrain() {
    drains.fetch_add(1, std::memory_order_relaxed);
}

std::string SampleReport(u64 game_frames) {
    std::scoped_lock lock{sample_mutex};
    const auto now = std::chrono::steady_clock::now();
    const bool first = last_sample == std::chrono::steady_clock::time_point{};
    const double window_ms =
        first ? 0.0
              : std::chrono::duration<double, std::milli>(now - last_sample).count();
    last_sample = now;

    const u64 gpu_ns = host_gpu_ns.exchange(0, std::memory_order_relaxed);
    const u64 gpu_samples = host_gpu_samples.exchange(0, std::memory_order_relaxed);
    const u64 frames = game_frames == 0 ? 1 : game_frames;
    const auto take = [](std::atomic<u64>& value) {
        return value.exchange(0, std::memory_order_relaxed);
    };
    const u64 gpu_wait = take(wait_ns[static_cast<size_t>(Role::Gpu)]);
    const u64 gpu_wait_n = take(wait_count[static_cast<size_t>(Role::Gpu)]);
    const u64 present_wait = take(wait_ns[static_cast<size_t>(Role::Present)]);
    const u64 worker_wait = take(wait_ns[static_cast<size_t>(Role::Worker)]);
    const u64 other_wait = take(wait_ns[static_cast<size_t>(Role::Other)]);
    take(wait_count[static_cast<size_t>(Role::Present)]);
    take(wait_count[static_cast<size_t>(Role::Worker)]);
    take(wait_count[static_cast<size_t>(Role::Other)]);
    const u64 frame_wait = take(present_frame_wait_ns);
    const u64 submit_count = take(submits);
    const u64 early_count = take(early_submits);
    const u64 drain_count = take(drains);

    if (first || window_ms <= 0.0) {
        return {};
    }
    // gpu_busy: share of wall time the host GPU spent executing our submissions.
    // gpu_ms/frame: host GPU time per game frame. Above ~16.6 ms the host GPU alone cannot hold
    // 60 FPS. Below that, drops come from waits: gpu_wait is the GPU thread blocked on host GPU
    // completion, frame_wait the GPU thread blocked on a free presentation frame.
    return fmt::format(
        "host_gpu=[busy={:.0f}% gpu_ms/frame={:.2f} timed_submits={} submits/frame={:.1f} "
        "early={} drains={}] waits_ms/frame=[gpu_thread={:.2f} (n={}) present_frame={:.2f} "
        "present_thread={:.2f} worker={:.2f} other={:.2f}]",
        gpu_samples == 0 ? 0.0 : Ms(gpu_ns) * 100.0 / window_ms,
        gpu_samples == 0 ? 0.0 : Ms(gpu_ns) / static_cast<double>(frames), gpu_samples,
        static_cast<double>(submit_count) / static_cast<double>(frames), early_count, drain_count,
        Ms(gpu_wait) / static_cast<double>(frames), gpu_wait_n,
        Ms(frame_wait) / static_cast<double>(frames), Ms(present_wait) / static_cast<double>(frames),
        Ms(worker_wait) / static_cast<double>(frames), Ms(other_wait) / static_cast<double>(frames));
}

} // namespace Common::GpuFrameStats
