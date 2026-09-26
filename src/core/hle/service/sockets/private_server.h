// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Service::Sockets {

/**
 * The address a guest DNS lookup for @p host should be answered with, when a private replacement
 * for Nintendo's servers (such as SwitchNet) is configured, or nullopt to resolve it normally.
 *
 * Only Nintendo-owned domains are redirected, so turning this on can never send a game's traffic
 * for an unrelated service to the private server. Every Nintendo host goes there, including ones
 * the server does not serve: those then fail against the private server rather than reaching
 * Nintendo, which is the point of running one.
 */
std::optional<std::string> PrivateServerRedirect(std::string_view host);

} // namespace Service::Sockets
