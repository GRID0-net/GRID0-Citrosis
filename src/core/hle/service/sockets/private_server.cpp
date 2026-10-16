// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>

#include "common/logging.h"
#include "common/settings.h"
#include "core/hle/service/sockets/private_server.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/hle_ipc.h"
#include "core/memory.h"

namespace Service::Sockets {

namespace {

// Domains Nintendo operates. The same list SwitchNet's own DNS configuration treats as Nintendo's
// (internal/nintendo/hosts.go), so the emulator and a console behind SwitchNet's resolver agree on
// which names belong to the private server.
constexpr std::array<std::string_view, 33> NintendoDomains{
    "nintendo.com",      "nintendo.net",        "nintendo.jp",        "nintendo.co.jp",
    "nintendo.co.uk",    "nintendo-europe.com", "nintendowifi.net",   "nintendo.es",
    "nintendo.co.kr",    "nintendo.tw",         "nintendo.com.hk",    "nintendo.com.au",
    "nintendo.co.nz",    "nintendo.at",         "nintendo.be",        "nintendods.cz",
    "nintendo.dk",       "nintendo.de",         "nintendo.fi",        "nintendo.fr",
    "nintendo.gr",       "nintendo.hu",         "nintendo.it",        "nintendo.nl",
    "nintendo.no",       "nintendo.pt",         "nintendo.ru",        "nintendo.co.za",
    "nintendo.se",       "nintendo.ch",         "nintendoswitch.com", "nintendoswitch.com.cn",
    "nintendoswitch.cn",
};

std::string Normalise(std::string_view host) {
    std::string name{host};
    while (!name.empty() && name.back() == '.') {
        name.pop_back();
    }
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

bool IsNintendoDomain(std::string_view name) {
    return std::any_of(NintendoDomains.begin(), NintendoDomains.end(), [name](auto domain) {
        if (name == domain) {
            return true;
        }
        return name.size() > domain.size() && name.ends_with(domain) &&
               name[name.size() - domain.size() - 1] == '.';
    });
}

} // namespace

std::string PrivateServerAddress() {
    std::string address = Settings::values.private_server_address.GetValue();
    if (!address.empty()) {
        return address;
    }
    // A config saved before the GRID0+ defaults existed holds an explicit empty address, which
    // overrides them. With a GRID0+ login set, use that login's server: signing in there while
    // the game itself talks to Nintendo fails with 2321-4992.
    if (Settings::values.switchnet_username.GetValue().empty()) {
        return {};
    }
    address = Settings::values.switchnet_server.GetValue();
    if (const auto colon = address.find(':'); colon != std::string::npos) {
        address.resize(colon);
    }
    return address;
}

std::optional<std::string> PrivateServerRedirect(std::string_view host) {
    const std::string primary = PrivateServerAddress();
    if (primary.empty()) {
        return std::nullopt;
    }

    const std::string name = Normalise(host);
    if (!IsNintendoDomain(name)) {
        return std::nullopt;
    }

    // Pia probes nncs1 and nncs2 as two servers, and one NAT-check message has to be answered
    // from a different address: the same address makes the console de-duplicate the probe, and
    // matchmaking then finds matches that never start.
    if (name.starts_with("nncs2")) {
        std::string secondary = Settings::values.private_server_nat_secondary_address.GetValue();
        if (secondary.empty() && primary == Settings::Grid0DefaultAddress) {
            secondary = std::string(Settings::Grid0DefaultNatSecondaryAddress);
        }
        if (!secondary.empty()) {
            return secondary;
        }
        LOG_WARNING(Network,
                    "{} redirected to the primary private server address because no NAT-check "
                    "secondary address is set; Pia NAT detection will not complete",
                    host);
    }

    return primary;
}

namespace {
// Enough for a connection to be opened, its TLS handshake and its first requests, with room
// for a retry; a poll that is re-checked while deferred is not counted until it returns.
constexpr int TraceBudget = 256;
std::atomic<int> trace_remaining{0};
std::atomic<bool> persistent_trace{false};
std::mutex trace_mutex;
std::chrono::steady_clock::time_point trace_window{};
int window_remaining{};
constexpr int PersistentTraceBudget = 512;
} // namespace

void ArmPrivateServerTrace() {
    trace_remaining.store(TraceBudget, std::memory_order_relaxed);
}

void SetPrivateServerTraceEnabled(bool enabled) {
    std::scoped_lock lock{trace_mutex};
    trace_window = {};
    window_remaining = 0;
    persistent_trace.store(enabled, std::memory_order_relaxed);
}

bool NetworkDiagnosticsEnabled() {
    return persistent_trace.load(std::memory_order_relaxed);
}

bool TakePrivateServerTrace() {
    if (persistent_trace.load(std::memory_order_relaxed)) {
        std::scoped_lock lock{trace_mutex};
        const auto now = std::chrono::steady_clock::now();
        if (now - trace_window >= std::chrono::seconds(1)) {
            trace_window = now;
            window_remaining = PersistentTraceBudget;
        }
        if (window_remaining == 0)
            return false;
        --window_remaining;
        return true;
    }
    int remaining = trace_remaining.load(std::memory_order_relaxed);
    while (remaining > 0) {
        if (trace_remaining.compare_exchange_weak(remaining, remaining - 1,
                                                  std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

namespace {
std::atomic<s64> npln_trace_until_ns{};
std::mutex npln_trace_mutex;
std::array<u64, 8> npln_threads{};
std::array<bool, 8> npln_stack_logged{};
u32 npln_svc_budget{};
u32 npln_reply_budget{};
u32 npln_trace_epoch{};

s64 NplnTraceNow() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool NplnTraceActive() {
    s64 until = npln_trace_until_ns.load(std::memory_order_relaxed);
    if (until == 0) {
        return false;
    }
    if (NplnTraceNow() < until) {
        return true;
    }
    // Return to an atomic-load-only fast path after expiry. Do not clear a newer epoch.
    npln_trace_until_ns.compare_exchange_strong(until, 0, std::memory_order_relaxed);
    return false;
}
} // namespace

void ArmNplnGuestTrace(Kernel::KThread& requester) {
    std::scoped_lock lock{npln_trace_mutex};
    npln_threads.fill(0);
    npln_stack_logged.fill(false);
    npln_threads[0] = requester.GetThreadId();
    npln_svc_budget = 192;
    npln_reply_budget = 24;
    ++npln_trace_epoch;
    npln_trace_until_ns.store(NplnTraceNow() + 5'000'000'000LL, std::memory_order_relaxed);
    LOG_WARNING(Network, "GRID0+ guest trace epoch={} DNS tid={} window=5s svc_budget=192",
                npln_trace_epoch, requester.GetThreadId());
}

void WatchNplnGuestThread(Kernel::KThread& thread) {
    if (!NplnTraceActive()) {
        return;
    }
    std::scoped_lock lock{npln_trace_mutex};
    const u64 tid = thread.GetThreadId();
    for (auto& watched : npln_threads) {
        if (watched == tid) {
            return;
        }
        if (watched == 0) {
            watched = tid;
            return;
        }
    }
}

void TraceNplnGuestSvc(Core::System& system, u32 svc, bool returning) {
    // No context reads, memory probes or locks outside the short diagnostic window.
    if (!NplnTraceActive()) {
        return;
    }
    // Thread creation/start/exit/sleep, synchronization, IPC and address waits.
    if ((svc < 0x8 || svc > 0xb) && (svc < 0x16 || svc > 0x1d) &&
        svc != 0x21 && svc != 0x22 && svc != 0x34 && svc != 0x35) {
        return;
    }
    auto& kernel = system.Kernel();
    auto& process = Kernel::GetCurrentProcess(kernel);
    if (!process.Is64Bit() || process.GetProgramId() != 0x0100C2500FC20000ULL) {
        return;
    }
    const u64 tid = Kernel::GetCurrentThread(kernel).GetThreadId();
    bool log_stack{};
    u32 epoch{};
    {
        std::scoped_lock lock{npln_trace_mutex};
        const auto it = std::find(npln_threads.begin(), npln_threads.end(), tid);
        if (it == npln_threads.end() || npln_svc_budget == 0) {
            return;
        }
        --npln_svc_budget;
        epoch = npln_trace_epoch;
        const auto index = static_cast<size_t>(it - npln_threads.begin());
        log_stack = !npln_stack_logged[index];
        npln_stack_logged[index] = true;
    }
    // Called only at the SVC boundary, with guest execution stopped. Read the live CPU
    // context into stack storage; do not overwrite the thread's saved context or registers.
    Kernel::Svc::ThreadContext context{};
    process.GetArmInterface(kernel.CurrentPhysicalCoreIndex())->GetContext(context);
    const u64 key = returning ? 0 :
        (svc == 0x18 || svc == 0x1a || svc == 0x1c ? context.r[1] :
         svc == 0x21 || svc == 0x16 || svc == 0x9 ? context.r[0] : 0);
    const s64 timeout = returning ? 0 :
        static_cast<s64>(svc == 0xb ? context.r[0] :
                         svc == 0x18 || svc == 0x1c || svc == 0x34 ? context.r[3] : 0);
    const u64 return_x1 = returning && (svc == 0x8 || svc == 0x18) ? context.r[1] : 0;
    LOG_WARNING(Network,
                "GRID0+ guest svc epoch={} tid={} svc={:#x} phase={} pc={:#x} lr={:#x} "
                "sp={:#x} key={:#x} timeout_ns={} return_x0={:#x} return_x1={:#x}",
                epoch, tid, svc, returning ? "return" : "enter", context.pc, context.lr,
                context.sp, key, timeout, returning ? context.r[0] : 0, return_x1);
    if (log_stack) {
        std::array<u64, 6> callers{};
        u64 fp = context.fp;
        for (auto& caller : callers) {
            // Only frame records within the next 64 KiB of the current stack, monotonically
            // increasing and 16-byte aligned. No strings, payloads or arbitrary data dumps.
            if (fp < context.sp || fp - context.sp > 65536 || (fp & 15) != 0) {
                break;
            }
            std::array<u64, 2> frame{};
            if (!process.GetMemory().ReadBlockUnsafe(fp, frame.data(), sizeof(frame)) ||
                frame[0] <= fp) {
                break;
            }
            caller = frame[1];
            fp = frame[0];
        }
        LOG_WARNING(Network,
                    "GRID0+ guest callers epoch={} tid={} lr={:#x} frames="
                    "{:#x},{:#x},{:#x},{:#x},{:#x},{:#x}", epoch, tid, context.lr,
                    callers[0], callers[1], callers[2], callers[3], callers[4], callers[5]);
    }
}

void TraceNplnGuestReply(HLERequestContext& ctx, std::string_view service) {
    if (!NplnTraceActive() || ctx.IsTipc() || ctx.GetIsDeferred()) {
        return;
    }
    const bool dns = service == "sfdnsres" && ctx.GetCommand() == 12;
    const bool bsd = service.starts_with("bsd:") &&
                     (ctx.GetCommand() == 6 || ctx.GetCommand() == 24 ||
                      ctx.GetCommand() == 25 || ctx.GetCommand() == 26);
    if (!dns && !bsd) {
        return;
    }
    u32 epoch{};
    {
        std::scoped_lock lock{npln_trace_mutex};
        if (npln_reply_budget == 0) {
            return;
        }
        --npln_reply_budget;
        epoch = npln_trace_epoch;
    }
    std::array<u32, 6> words{};
    const auto address = ctx.GetThread().GetTlsAddress() + ctx.GetDataPayloadOffset() * sizeof(u32);
    const bool readable = ctx.GetMemory().ReadBlockUnsafe(address, words.data(),
                                                        (dns ? 6 : 4) * sizeof(u32));
    LOG_WARNING(Network,
                "GRID0+ guest IPC reply epoch={} tid={} service={} cmd={} readable={} "
                "result={:#x}:{:#x} output={:#x},{:#x},{:#x},{:#x}", epoch,
                ctx.GetThread().GetThreadId(), service, static_cast<u32>(ctx.GetCommand()), readable,
                words[1], words[0], words[2], words[3], words[4], words[5]);
}

} // namespace Service::Sockets
