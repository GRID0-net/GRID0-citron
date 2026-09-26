// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::ERPT {

class ErrorReportContext final : public ServiceFramework<ErrorReportContext> {
public:
    explicit ErrorReportContext(Core::System& system_) : ServiceFramework{system_, "erpt:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ErrorReportContext::SubmitContext>, "SubmitContext"},
            {1, C<&ErrorReportContext::CreateReportV0>, "CreateReportV0"},
            {2, &ErrorReportContext::RecordOnly, "SetInitialLaunchSettingsCompletionTime"},
            {3, &ErrorReportContext::RecordOnly, "ClearInitialLaunchSettingsCompletionTime"},
            {4, &ErrorReportContext::RecordOnly, "UpdatePowerOnTime"},
            {5, &ErrorReportContext::RecordOnly, "UpdateAwakeTime"},
            {6, &ErrorReportContext::RecordOnly, "SubmitMultipleCategoryContext"},
            {7, &ErrorReportContext::RecordOnly, "UpdateApplicationLaunchTime"},
            {8, &ErrorReportContext::RecordOnly, "ClearApplicationLaunchTime"},
            {9, nullptr, "SubmitAttachment"},
            {10, nullptr, "CreateReportWithAttachments"},
            {11, C<&ErrorReportContext::CreateReportV1>, "CreateReportV1"},
            {12, C<&ErrorReportContext::CreateReport>, "CreateReport"},
            {20, &ErrorReportContext::RecordOnly, "RegisterRunningApplet"},
            {21, &ErrorReportContext::RecordOnly, "UnregisterRunningApplet"},
            {22, &ErrorReportContext::RecordOnly, "UpdateAppletSuspendedDuration"},
            {30, &ErrorReportContext::RecordOnly, "InvalidateForcedShutdownDetection"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    // Bookkeeping the real service keeps only so it can be written into error reports: launch,
    // power-on and awake times, running applets. Nothing reads it back here, so acknowledging is
    // the whole implementation. Left unimplemented, the first one called is fatal -- the error
    // applet calls UpdateAwakeTime while showing a game's error, so the game froze on its loading
    // screen instead of showing the error it had hit.
    void RecordOnly(HLERequestContext& ctx) {
        LOG_DEBUG(Service_SET, "called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    Result SubmitContext(InBuffer<BufferAttr_HipcMapAlias> context_entry,
                         InBuffer<BufferAttr_HipcMapAlias> field_list) {
        LOG_WARNING(Service_SET, "(STUBBED) called, context_entry_size={}, field_list_size={}",
                    context_entry.size(), field_list.size());
        R_SUCCEED();
    }

    Result CreateReportV0(u32 report_type, InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}", report_type);
        R_SUCCEED();
    }

    Result CreateReportV1(u32 report_type, u32 unknown,
                          InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}, unknown={:#x}", report_type,
                    unknown);
        R_SUCCEED();
    }

    Result CreateReport(u32 report_type, u32 unknown, u32 create_report_option_flag,
                        InBuffer<BufferAttr_HipcMapAlias> context_entry,
                        InBuffer<BufferAttr_HipcMapAlias> report_list,
                        InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(
            Service_SET,
            "(STUBBED) called, report_type={:#x}, unknown={:#x}, create_report_option_flag={:#x}",
            report_type, unknown, create_report_option_flag);
        R_SUCCEED();
    }
};

class ErrorReportSession final : public ServiceFramework<ErrorReportSession> {
public:
    explicit ErrorReportSession(Core::System& system_) : ServiceFramework{system_, "erpt:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "OpenReport"},
            {1, nullptr, "OpenManager"},
            {2, nullptr, "OpenAttachment"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("erpt:c", std::make_shared<ErrorReportContext>(system));
    server_manager->RegisterNamedService("erpt:r", std::make_shared<ErrorReportSession>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ERPT
