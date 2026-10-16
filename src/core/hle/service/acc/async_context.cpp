// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/acc/switchnet_account.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/private_server.h"

namespace Service::Account {
IAsyncContext::IAsyncContext(Core::System& system_)
    : ServiceFramework{system_, "IAsyncContext"}, service_context{system_, "IAsyncContext"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IAsyncContext::GetSystemEvent, "GetSystemEvent"},
        {1, &IAsyncContext::Cancel, "Cancel"},
        {2, &IAsyncContext::HasDone, "HasDone"},
        {3, &IAsyncContext::GetResult, "GetResult"},
    };
    // clang-format on

    RegisterHandlers(functions);

    completion_event = service_context.CreateEvent("IAsyncContext:CompletionEvent");
}

IAsyncContext::~IAsyncContext() {
    service_context.CloseEvent(completion_event);
}

void IAsyncContext::GetSystemEvent(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    if (SwitchNet::IsConfigured() && Sockets::TakePrivateServerTrace()) {
        LOG_WARNING(Service_ACC, "GRID0+ account async GetSystemEvent: complete={} domain={}",
                    is_complete.load(), ctx.GetManager()->IsDomain());
    }

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, completion_event->GetReadableEvent());
}

void IAsyncContext::Cancel(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    Cancel();
    MarkComplete();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IAsyncContext::HasDone(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    is_complete.store(IsComplete());
    if (SwitchNet::IsConfigured() && Sockets::TakePrivateServerTrace()) {
        LOG_WARNING(Service_ACC, "GRID0+ account async HasDone: complete={}", is_complete.load());
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_complete.load());
}

void IAsyncContext::GetResult(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    const auto result = GetResult();
    if (SwitchNet::IsConfigured() && Sockets::TakePrivateServerTrace()) {
        LOG_WARNING(Service_ACC, "GRID0+ account async GetResult: result={:#x}", result.raw);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IAsyncContext::MarkComplete() {
    is_complete.store(true);
    completion_event->Signal(system.Kernel());
}

} // namespace Service::Account
