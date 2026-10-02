// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

#include "common/common_types.h"

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
 * from the GRID0+ Discord bot (/register) (POST /login), and hands the game the token the server
 * signed. No Nintendo credential is presented, verified or forged.
 *
 * Returns nullopt when not configured or when the login fails. A failure is logged once per
 * distinct reason, with the actual cause.
 */
std::optional<std::string> GetIdToken();

/**
 * The signed-in user's network service account id: the id token's subject, which is how the
 * server's NPLN names this user and every friend relationship. nullopt when not logged in.
 */
std::optional<u64> GetNetworkServiceAccountId();

/// The BAAS access token from the same login, for the server's own emulator API.
std::optional<std::string> GetAccessToken();

struct Response {
    int status;
    std::string body;
};

/**
 * A request to the server's BAAS host with the access token, for the emulator API
 * (/emulator/v1/...). nullopt when not logged in or the server cannot be reached.
 */
std::optional<Response> Request(const std::string& method, const std::string& path,
                                const std::string& json_body = {});

/// GETs an https URL on a Nintendo host the server answers for (a profile picture).
std::optional<std::string> Fetch(const std::string& url);

} // namespace Service::Account::SwitchNet
