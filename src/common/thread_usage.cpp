// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "common/thread_usage.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#include <time.h>
#define CITROSIS_THREAD_CPU_CLOCK 1
#endif

namespace Common::ThreadUsage {

namespace {

#if defined(_WIN32)
using NativeClock = HANDLE;

bool OpenCurrent(NativeClock& out) {
    return DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &out,
                           THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0) != 0;
}

void Close(NativeClock clock) {
    CloseHandle(clock);
}

bool ReadNs(NativeClock clock, u64& out) {
    FILETIME creation, exit, kernel, user;
    if (!GetThreadTimes(clock, &creation, &exit, &kernel, &user)) {
        return false;
    }
    const auto to_u64 = [](const FILETIME& t) {
        return (static_cast<u64>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    out = (to_u64(kernel) + to_u64(user)) * 100; // 100 ns units
    return true;
}
#elif defined(CITROSIS_THREAD_CPU_CLOCK)
using NativeClock = clockid_t;

bool OpenCurrent(NativeClock& out) {
    return pthread_getcpuclockid(pthread_self(), &out) == 0;
}

void Close(NativeClock) {}

bool ReadNs(NativeClock clock, u64& out) {
    timespec ts{};
    if (clock_gettime(clock, &ts) != 0) {
        return false;
    }
    out = static_cast<u64>(ts.tv_sec) * 1'000'000'000ULL + static_cast<u64>(ts.tv_nsec);
    return true;
}
#else
using NativeClock = int;

bool OpenCurrent(NativeClock&) {
    return false;
}

void Close(NativeClock) {}

bool ReadNs(NativeClock, u64&) {
    return false;
}
#endif

struct Entry {
    u64 id;
    std::string name;
    NativeClock clock;
    u64 last_ns;
    std::chrono::steady_clock::time_point last_wall;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
u64 g_next_id = 1;

} // Anonymous namespace

Registration::Registration(std::string name) : id{0} {
    NativeClock clock{};
    if (!OpenCurrent(clock)) {
        return;
    }
    u64 now_ns = 0;
    ReadNs(clock, now_ns);
    std::scoped_lock lock{g_mutex};
    id = g_next_id++;
    g_entries.push_back(Entry{
        .id = id,
        .name = std::move(name),
        .clock = clock,
        .last_ns = now_ns,
        .last_wall = std::chrono::steady_clock::now(),
    });
}

Registration::~Registration() {
    if (id == 0) {
        return;
    }
    // Removed before the thread exits, so the sampler never reads a dead thread's clock.
    std::scoped_lock lock{g_mutex};
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if (it->id == id) {
            Close(it->clock);
            g_entries.erase(it);
            break;
        }
    }
}

std::string SampleReport() {
    std::scoped_lock lock{g_mutex};
    std::string out;
    const auto now = std::chrono::steady_clock::now();
    for (auto& entry : g_entries) {
        u64 cpu_ns = 0;
        if (!ReadNs(entry.clock, cpu_ns)) {
            continue;
        }
        const auto wall_ns = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - entry.last_wall).count());
        const u64 used = cpu_ns >= entry.last_ns ? cpu_ns - entry.last_ns : 0;
        entry.last_ns = cpu_ns;
        entry.last_wall = now;
        if (wall_ns == 0) {
            continue;
        }
        if (!out.empty()) {
            out += ' ';
        }
        out += fmt::format("{}={:.0f}%", entry.name,
                           100.0 * static_cast<double>(used) / static_cast<double>(wall_ns));
    }
    return out;
}

} // namespace Common::ThreadUsage
