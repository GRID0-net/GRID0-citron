// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/server_manager.h"
#include "core/hle/service/sockets/bsd.h"
#include "core/hle/service/sockets/bsdnu.h"
#include "core/hle/service/sockets/deferred_poll_waker.h"
#include "core/hle/service/sockets/dnspriv.h"
#include "core/hle/service/sockets/ethc.h"
#include "core/hle/service/sockets/nsd.h"
#include "core/hle/service/sockets/sfdnsres.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    // Shared by the three bsd services, which one server manager (and so one deferral event)
    // serves. Only the services hold it, so it is gone -- and its thread joined -- before the
    // server manager closes the event.
    Kernel::KEvent* deferral_event{};
    server_manager->ManageDeferral(&deferral_event);
    const auto waker = std::make_shared<DeferredPollWaker>(deferral_event);

    for (const char* name : {"bsd:a", "bsd:s", "bsd:u"}) {
        auto bsd = std::make_shared<BSD>(system, name);
        bsd->SetDeferredPollWaker(waker);
        server_manager->RegisterNamedService(name, std::move(bsd));
    }
    server_manager->RegisterNamedService("bsd:nu", std::make_shared<BSDNU>(system));
    server_manager->RegisterNamedService("bsdcfg", std::make_shared<BSDCFG>(system));
    server_manager->RegisterNamedService("dns:priv", std::make_shared<DNSPRIV>(system));
    server_manager->RegisterNamedService("ethc:c", std::make_shared<ETHC_C>(system));
    server_manager->RegisterNamedService("ethc:i", std::make_shared<ETHC_I>(system));
    server_manager->RegisterNamedService("nsd:a", std::make_shared<NSD>(system, "nsd:a"));
    server_manager->RegisterNamedService("nsd:u", std::make_shared<NSD>(system, "nsd:u"));
    server_manager->RegisterNamedService("sfdnsres", std::make_shared<SFDNSRES>(system));
    server_manager->StartAdditionalHostThreads("bsdsocket", 2);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Sockets
