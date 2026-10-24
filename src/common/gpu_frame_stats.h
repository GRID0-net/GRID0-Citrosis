// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "common/common_types.h"

/**
 * Host GPU timing and blocking-wait accounting for the performance log (Citrosis).
 *
 * Thread CPU usage alone cannot separate "the host GPU is too slow" from "a thread is blocked on
 * the host GPU or on presentation". These counters fill that gap:
 *  - host GPU busy time, from timestamp queries around every queue submission;
 *  - time each emulator role spent blocked on host GPU completion (timeline semaphore waits);
 *  - time the GPU thread spent waiting for a free presentation frame;
 *  - full queue drains (Scheduler::Finish) and submissions, including early mid-frame ones.
 * All entry points are a relaxed atomic add; nothing is recorded per draw.
 */
namespace Common::GpuFrameStats {

enum class Role : u8 {
    Other,
    Gpu,     ///< GPU command processing thread.
    Present, ///< Vulkan presentation thread.
    Worker,  ///< Vulkan recording/submission worker.
    Count,
};

/// Tags the calling thread for wait attribution.
void SetThreadRole(Role role);

/// Host GPU execution time of one submission, in nanoseconds.
void AddHostGpuTime(u64 ns);

/// Time the calling thread was blocked waiting for the host GPU to reach a tick.
void AddHostWait(u64 ns);

/// Time the GPU thread waited for a presentation frame to become free.
void AddPresentFrameWait(u64 ns);

/// One queue submission. @p early is true for submissions made mid-frame to start the host GPU
/// sooner.
void AddSubmit(bool early);

/// One full queue drain (submit and wait for completion).
void AddDrain();

/// Returns "gpu_busy=NN% gpu_ms_per_frame=X ..." covering the time since the previous call.
/// @p game_frames is the number of game frames presented in that window.
std::string SampleReport(u64 game_frames);

} // namespace Common::GpuFrameStats
