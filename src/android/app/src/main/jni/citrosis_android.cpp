// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>

#include <android/api-level.h>
#include <jni.h>
#include <malloc.h>
#include <nlohmann/json.hpp>

#include "citrosis_android.h"
#include "common/android/android_common.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include "common/logging.h"
#include "common/memory_budget.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/hle/service/acc/switchnet_account.h"
#include "core/hle/service/friend/grid0_friends.h"
#include "core/internal_network/network_interface.h"

namespace Citrosis::Android {

using namespace Common::Literals;
using Common::MemoryBudget::Pressure;
using Clock = std::chrono::steady_clock;

namespace {

// ---- Memory pressure ------------------------------------------------------------------------
//
// Android 14+ no longer sends the onTrimMemory RUNNING_* levels to foreground apps, so a
// foreground emulator gets no warning before lmkd kills it. MemAvailable from /proc/meminfo is
// readable by apps and tracks what lmkd looks at closely enough: poll it, and also take whatever
// onTrimMemory still delivers.

std::mutex monitor_mutex;
std::condition_variable monitor_cv;
std::thread monitor_thread;
bool monitor_stop = false;

std::atomic<u8> trim_level{0};
std::atomic<s64> trim_until_ns{0};

u64 PhysicalMemory() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0) {
        return 0;
    }
    return static_cast<u64>(pages) * static_cast<u64>(page_size);
}

/// MemAvailable in bytes, or 0 when it cannot be read.
/// This process's resident memory (RSS) in bytes, or 0.
u64 ReadOwnRss() {
    std::FILE* file = std::fopen("/proc/self/statm", "re");
    if (file == nullptr) {
        return 0;
    }
    unsigned long long size = 0, resident = 0;
    const int read = std::fscanf(file, "%llu %llu", &size, &resident);
    std::fclose(file);
    return read == 2 ? resident * static_cast<u64>(sysconf(_SC_PAGE_SIZE)) : 0;
}

u64 ReadMemAvailable() {
    std::FILE* file = std::fopen("/proc/meminfo", "re");
    if (file == nullptr) {
        return 0;
    }
    char line[128];
    u64 available_kib = 0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        unsigned long long value = 0;
        if (std::sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            available_kib = value;
            break;
        }
    }
    std::fclose(file);
    return available_kib * 1024;
}

const char* PressureName(Pressure pressure) {
    switch (pressure) {
    case Pressure::None:
        return "none";
    case Pressure::Moderate:
        return "moderate";
    case Pressure::Critical:
        return "critical";
    }
    return "?";
}

void ReportProcessMemory() {
    // RSS can count guest aliases more than once. Separate shared guest mappings from
    // native heap growth, and report swap rather than treating all RSS as unique RAM.
    std::FILE* file = std::fopen("/proc/self/status", "re");
    if (!file) {
        return;
    }
    unsigned long long anon = 0, shmem = 0, mapped_file = 0, swap = 0;
    char line[128];
    while (std::fgets(line, sizeof(line), file)) {
        if (std::sscanf(line, "RssAnon: %llu kB", &anon) == 1 ||
            std::sscanf(line, "RssShmem: %llu kB", &shmem) == 1 ||
            std::sscanf(line, "RssFile: %llu kB", &mapped_file) == 1 ||
            std::sscanf(line, "VmSwap: %llu kB", &swap) == 1) {
            continue;
        }
    }
    std::fclose(file);
    LOG_WARNING(Frontend,
                "Memory mappings: anonymous {} MiB, shared {} MiB, file {} MiB, "
                "anonymous swap {} MiB",
                anon / 1024, shmem / 1024, mapped_file / 1024, swap / 1024);
}

void MonitorLoop(u64 total) {
    Common::SetCurrentThreadName("MemoryMonitor");

    // A later run reached 635 MiB available then died at 4817 MiB RSS. Keep a larger
    // reserve for loading bursts rather than interpreting 640 MiB as a safe floor.
    const u64 moderate = std::max<u64>(1536_MiB, total / 6);
    const u64 critical = std::max<u64>(1_GiB, total / 8);
    // Leave a level only when comfortably above its threshold, so the caches do not flap.
    const auto above = [](u64 available, u64 threshold) {
        return available > threshold + threshold / 4;
    };

    Pressure current = Pressure::None;
    auto next_report = Clock::now();
    auto next_purge = Clock::now();
    std::unique_lock lock{monitor_mutex};
    while (!monitor_stop) {
        const u64 available = ReadMemAvailable();
        Common::MemoryBudget::SetAvailableMemory(available);
        Pressure next = current;
        if (available != 0) {
            if (available < critical) {
                next = Pressure::Critical;
            } else if (available < moderate) {
                next = current == Pressure::Critical && !above(available, critical)
                           ? Pressure::Critical
                           : Pressure::Moderate;
            } else if (current == Pressure::None || above(available, moderate)) {
                next = Pressure::None;
            }
        }

        // onTrimMemory latches a level for a few seconds.
        const s64 now_ns = Clock::now().time_since_epoch().count();
        if (now_ns < trim_until_ns.load(std::memory_order_relaxed)) {
            next =
                std::max(next, static_cast<Pressure>(trim_level.load(std::memory_order_relaxed)));
        }

        // Logged at Warning so it shows with the default log filter: when Android kills the
        // process for memory, these lines are the only record of how close it was.
        if (next != current) {
            LOG_WARNING(Frontend, "Memory pressure {} -> {} (MemAvailable {} MiB, our RSS {} MiB)",
                        PressureName(current), PressureName(next), available / 1_MiB,
                        ReadOwnRss() / 1_MiB);
            current = next;
            Common::MemoryBudget::SetPressure(current);
        }
        const auto now = Clock::now();
        const bool performance = Settings::values.android_vulkan_performance.GetValue();
        const bool purge_needed = performance ? next == Pressure::Critical && available < 512_MiB
                                              : next != Pressure::None;
        if (Common::MemoryBudget::GetPhysicalMemory() != 0 && purge_needed && now >= next_purge) {
            // Release free allocator pages left behind by loader/shader/scratch work.
            // Only free memory is purged; live allocations and guest mappings are preserved.
            const int option = next == Pressure::Critical && android_get_device_api_level() >= 34
                                   ? M_PURGE_ALL
                                   : M_PURGE;
            const u64 before = ReadOwnRss();
            const int purged = mallopt(option, 0);
            LOG_WARNING(Frontend, "Native heap purge: accepted {}, RSS {} -> {} MiB", purged,
                        before / 1_MiB, ReadOwnRss() / 1_MiB);
            next_purge = now + std::chrono::seconds(performance ? 10 : 2);
        }
        if (now >= next_report) {
            next_report = now + std::chrono::seconds(current == Pressure::None ? 5 : 1);
            LOG_WARNING(Frontend, "Memory: our RSS {} MiB, MemAvailable {} MiB of {} MiB",
                        ReadOwnRss() / 1_MiB, available / 1_MiB, total / 1_MiB);
            ReportProcessMemory();
        }

        // Poll faster while under pressure, when things change quickly.
        const auto period = current == Pressure::None ? std::chrono::milliseconds(250)
                                                      : std::chrono::milliseconds(100);
        monitor_cv.wait_for(lock, period, [] { return monitor_stop; });
    }
}

void StopMonitor() {
    {
        std::scoped_lock lock{monitor_mutex};
        monitor_stop = true;
    }
    monitor_cv.notify_all();
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
}

} // namespace

void OnEmulationStarting() {
    StopMonitor();

    Common::MemoryBudget::SetPressure(Pressure::None);
    const u64 total = PhysicalMemory();
    if (Settings::values.fit_caches_to_ram.GetValue() && total != 0) {
        Common::MemoryBudget::SetPhysicalMemory(total);
        // Prefer returning free pages to keeping a warm native heap during a game.
        mallopt(M_DECAY_TIME, Settings::values.android_vulkan_performance.GetValue() ? 1 : 0);
        if (android_get_device_api_level() >= 31) {
            mallopt(M_CACHE_COUNT_MAX, 2);
            mallopt(M_CACHE_SIZE_MAX, 1_MiB);
        }
        const auto limits = Common::MemoryBudget::GetLimits();
        LOG_WARNING(Frontend,
                    "RAM budget for {} MiB: textures {}/{} MiB, buffers {}/{} MiB, staging {} MiB "
                    "(expected/critical)",
                    total / 1_MiB, limits->texture_expected / 1_MiB,
                    limits->texture_critical / 1_MiB, limits->buffer_expected / 1_MiB,
                    limits->buffer_critical / 1_MiB, limits->staging_expected / 1_MiB);
    } else {
        Common::MemoryBudget::SetPhysicalMemory(0);
    }
    Common::MemoryBudget::SetPressure(Pressure::None);

    {
        std::scoped_lock lock{monitor_mutex};
        monitor_stop = false;
    }
    monitor_thread = std::thread(MonitorLoop, total);
}

void OnEmulationStopped() {
    StopMonitor();
    Common::MemoryBudget::SetPressure(Pressure::None);
    mallopt(M_DECAY_TIME, 1);
}

} // namespace Citrosis::Android

extern "C" {

// ComponentCallbacks2 levels.
static constexpr jint TRIM_MEMORY_RUNNING_MODERATE = 5;
static constexpr jint TRIM_MEMORY_RUNNING_LOW = 10;
static constexpr jint TRIM_MEMORY_RUNNING_CRITICAL = 15;
static constexpr jint TRIM_MEMORY_MODERATE = 60;
static constexpr jint TRIM_MEMORY_COMPLETE = 80;

JNIEXPORT void JNICALL Java_dev_lemon_lemon_1emu_NativeLibrary_onTrimMemory(JNIEnv* env,
                                                                            jobject obj,
                                                                            jint level) {
    using namespace Citrosis::Android;
    Pressure pressure = Pressure::None;
    if (level >= TRIM_MEMORY_COMPLETE || level == TRIM_MEMORY_RUNNING_CRITICAL) {
        pressure = Pressure::Critical;
    } else if (level >= TRIM_MEMORY_MODERATE || level == TRIM_MEMORY_RUNNING_LOW ||
               level == TRIM_MEMORY_RUNNING_MODERATE) {
        pressure = Pressure::Moderate;
    }
    if (pressure == Pressure::None) {
        return; // UI_HIDDEN / BACKGROUND: nothing to do while the game keeps running.
    }
    trim_level.store(static_cast<u8>(pressure), std::memory_order_relaxed);
    trim_until_ns.store((Clock::now() + std::chrono::seconds(5)).time_since_epoch().count(),
                        std::memory_order_relaxed);
    // Apply right away; the monitor keeps it for the latch period.
    if (pressure > Common::MemoryBudget::GetPressure()) {
        Common::MemoryBudget::SetPressure(pressure);
    }
    LOG_INFO(Frontend, "onTrimMemory({})", level);
}

JNIEXPORT void JNICALL Java_dev_lemon_lemon_1emu_NativeLibrary_setNetworkGateways(
    JNIEnv* env, jobject obj, jobjectArray jnames, jobjectArray jgateways) {
    std::vector<std::pair<std::string, u32>> gateways;
    const jsize count = std::min(env->GetArrayLength(jnames), env->GetArrayLength(jgateways));
    for (jsize i = 0; i < count; ++i) {
        auto* jname = static_cast<jstring>(env->GetObjectArrayElement(jnames, i));
        auto* jgateway = static_cast<jstring>(env->GetObjectArrayElement(jgateways, i));
        const std::string name = Common::Android::GetJString(env, jname);
        const std::string gateway = Common::Android::GetJString(env, jgateway);
        env->DeleteLocalRef(jname);
        env->DeleteLocalRef(jgateway);
        in_addr address{};
        if (!name.empty() && inet_pton(AF_INET, gateway.c_str(), &address) == 1) {
            gateways.emplace_back(name, address.s_addr);
        }
    }
    Network::SetPlatformGateways(std::move(gateways));
}

/// Interfaces the emulator can use, as "name\tIPv4" strings, for the interface picker.
JNIEXPORT jobjectArray JNICALL
Java_dev_lemon_lemon_1emu_NativeLibrary_getNetworkInterfaces(JNIEnv* env, jobject obj) {
    const auto interfaces = Network::GetAvailableNetworkInterfaces();
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(interfaces.size()),
                                              env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < interfaces.size(); ++i) {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &interfaces[i].ip_address, ip, sizeof(ip));
        jstring entry = Common::Android::ToJString(env, interfaces[i].name + '\t' + ip);
        env->SetObjectArrayElement(result, static_cast<jsize>(i), entry);
        env->DeleteLocalRef(entry);
    }
    return result;
}

/// Logs in to GRID0+ with the saved settings. Returns {"1" or "0", message}. Blocks on the
/// network: call it off the main thread.
JNIEXPORT jobjectArray JNICALL
Java_dev_lemon_lemon_1emu_NativeLibrary_grid0CheckLogin(JNIEnv* env, jobject obj) {
    const auto check = Service::Account::SwitchNet::CheckLogin();
    jobjectArray result = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
    jstring ok = Common::Android::ToJString(env, check.ok ? "1" : "0");
    jstring message = Common::Android::ToJString(env, check.message);
    env->SetObjectArrayElement(result, 0, ok);
    env->SetObjectArrayElement(result, 1, message);
    env->DeleteLocalRef(ok);
    env->DeleteLocalRef(message);
    return result;
}

// ---- GRID0+ friends ---------------------------------------------------------------------------
// Same data and actions as the Qt Friends window and the macOS app (macos/bridge.cpp), as JSON.
// Both calls block on the network: run them off the main thread.

static nlohmann::json SerializeFriend(const Service::Friend::Grid0::Friend& f, bool picture) {
    std::string icon;
    if (picture) {
        const auto image =
            Service::Friend::Grid0::ProfileImage(f.nsa_id, std::chrono::milliseconds(3000));
        if (image && !image->empty()) {
            auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "grid0-friends";
            std::error_code error;
            std::filesystem::create_directories(path, error);
            path /= std::to_string(f.nsa_id) + ".jpg";
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(image->data()),
                       static_cast<std::streamsize>(image->size()));
            if (file) {
                icon = path.string();
            }
        }
    }
    return nlohmann::json{{"id", std::to_string(f.nsa_id)},
                          {"name", f.nickname},
                          {"state", f.state},
                          {"app", f.app_id},
                          {"code", f.friend_code},
                          {"favorite", f.is_favorite},
                          {"icon", icon}};
}

JNIEXPORT jstring JNICALL Java_dev_lemon_lemon_1emu_NativeLibrary_grid0Friends(JNIEnv* env,
                                                                               jobject obj) {
    namespace Grid = Service::Friend::Grid0;
    nlohmann::json result;
    if (!Grid::IsEnabled()) {
        result["error"] = "not_signed_in";
        return Common::Android::ToJString(env, result.dump());
    }
    const auto me = Grid::FetchMe();
    if (!me) {
        result["error"] = "Could not sign in to GRID0+. Check your account and that the server "
                          "is reachable.";
        return Common::Android::ToJString(env, result.dump());
    }
    nlohmann::json friends = nlohmann::json::array();
    nlohmann::json incoming = nlohmann::json::array();
    nlohmann::json outgoing = nlohmann::json::array();
    const auto list = Grid::FetchNowBlocking();
    if (list) {
        for (const auto& f : *list) {
            friends.push_back(SerializeFriend(f, true));
        }
    }
    std::vector<Grid::FriendRequest> in, out;
    const bool requests_ok = Grid::FetchRequests(in, out);
    for (const auto& r : in) {
        incoming.push_back({{"id", r.id}, {"other", SerializeFriend(r.other, false)}});
    }
    for (const auto& r : out) {
        outgoing.push_back({{"id", r.id}, {"other", SerializeFriend(r.other, false)}});
    }
    result = {{"friends", friends},
              {"incoming", incoming},
              {"outgoing", outgoing},
              {"me", {{"name", me->nickname}, {"code", me->friend_code}}}};
    if (!list || !requests_ok) {
        result["warning"] = "Some GRID0+ information could not be refreshed. Try again.";
    }
    return Common::Android::ToJString(env, result.dump());
}

/// action: "send" (id = friend code), "accept"/"deny"/"cancel" (id = request id), "remove"
/// (id = NSA id). Returns an error message, or an empty string on success.
JNIEXPORT jstring JNICALL Java_dev_lemon_lemon_1emu_NativeLibrary_grid0FriendAction(
    JNIEnv* env, jobject obj, jstring jid, jstring jaction) {
    namespace Grid = Service::Friend::Grid0;
    const std::string id = Common::Android::GetJString(env, jid);
    const std::string action = Common::Android::GetJString(env, jaction);
    std::optional<std::string> error;
    if (action == "send") {
        error = Grid::SendRequest(id);
    } else if (action == "accept" || action == "deny" || action == "cancel") {
        error = Grid::SettleRequest(id, action);
    } else if (action == "remove") {
        try {
            error = Grid::RemoveFriend(std::stoull(id));
        } catch (...) {
            error = "Invalid friend ID.";
        }
    } else {
        error = "Unsupported friend action.";
    }
    return Common::Android::ToJString(env, error.value_or(""));
}

} // extern "C"
