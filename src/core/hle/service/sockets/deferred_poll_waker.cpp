// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include "common/thread.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/sockets/deferred_poll_waker.h"

namespace Service::Sockets {

DeferredPollWaker::DeferredPollWaker(Kernel::KEvent* deferral_event)
    : event{deferral_event}, thread{[this](std::stop_token stop) { Run(stop); }} {}

DeferredPollWaker::~DeferredPollWaker() {
    thread.request_stop();
    cv.notify_all();
}

void DeferredPollWaker::AddWaiter() {
    {
        std::scoped_lock lock{mutex};
        ++waiters;
    }
    cv.notify_all();
}

void DeferredPollWaker::RemoveWaiter() {
    std::scoped_lock lock{mutex};
    if (waiters > 0) {
        --waiters;
    }
}

void DeferredPollWaker::Wake() {
    event->Signal();
}

void DeferredPollWaker::Run(std::stop_token stop) {
    Common::SetCurrentThreadName("bsd:DeferredPoll");

    while (!stop.stop_requested()) {
        {
            std::unique_lock lock{mutex};
            Common::CondvarWait(cv, lock, stop, [this] { return waiters > 0; });
        }
        if (stop.stop_requested()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        event->Signal();
    }
}

} // namespace Service::Sockets
