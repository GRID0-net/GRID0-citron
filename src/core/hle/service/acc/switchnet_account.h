// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

namespace Service::Account::SwitchNet {

/**
 * Whether the emulator is set up to log in to a SwitchNet server: server, username and password
 * all set. Those three settings are the switch; there is no separate enable.
 */
bool IsConfigured();

/**
 * The BAAS id token the SwitchNet server signed for this user, logging in first when there is no
 * cached token or it is about to expire.
 *
 * A game asks the account service for this token before it can reach any online game server. On
 * a console the account sysmodule gets it by logging in with a device account; an emulator has no
 * device account, so this logs in the way a person does, with the username and password chosen
 * on the server's own /register page (POST /login), and hands the game the token the server
 * signed. No Nintendo credential is presented, verified or forged.
 *
 * Returns nullopt when not configured or when the login fails. A failure is logged once per
 * distinct reason, with the actual cause.
 */
std::optional<std::string> GetIdToken();

} // namespace Service::Account::SwitchNet
