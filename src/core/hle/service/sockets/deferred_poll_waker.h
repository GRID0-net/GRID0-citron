// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

namespace Kernel {
class KEvent;
}

namespace Service::Sockets {

/**
 * Re-runs the bsd polls whose replies are being held back.
 *
 * A poll that includes an eventfd -- in practice a gRPC event loop, which is what Splatoon 3's
 * online client runs on -- is woken by a write to that eventfd from another guest thread, and
 * both are requests to this same service. Blocking a service thread in such a poll can use up
 * every thread the service has while the write that would end the wait is still queued. So such a
 * poll checks once and, if nothing is ready, is deferred (HLERequestContext::SetIsDeferred); the
 * server manager re-runs deferred requests whenever its deferral event is signalled.
 *
 * Nothing signals that event when a host socket becomes readable, so while any poll is deferred
 * this signals it every millisecond -- a coarser tick would become the round-trip clock of the
 * guest's whole HTTP/2 transport -- and immediately on every eventfd write. With nothing deferred
 * its thread sleeps.
 */
class DeferredPollWaker {
public:
    explicit DeferredPollWaker(Kernel::KEvent* deferral_event);
    ~DeferredPollWaker();

    DeferredPollWaker(const DeferredPollWaker&) = delete;
    DeferredPollWaker& operator=(const DeferredPollWaker&) = delete;

    /// A poll has been deferred.
    void AddWaiter();
    /// A deferred poll has completed.
    void RemoveWaiter();
    /// Something a deferred poll may be waiting on has happened; re-run them now.
    void Wake();

private:
    void Run(std::stop_token stop);

    Kernel::KEvent* event;
    std::mutex mutex;
    std::condition_variable_any cv;
    int waiters = 0;
    std::jthread thread;
};

} // namespace Service::Sockets
