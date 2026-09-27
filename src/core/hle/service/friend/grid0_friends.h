// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

// Friends on a GRID0+ (SwitchNet) server, for the HLE friend service and the Friends window.
//
// A console's friends sysmodule keeps its own copy of the friend list, synced from BAAS. The
// emulator has none, so this keeps one: fetched from the server's emulator API with the account
// login the game already uses, refreshed in the background, and never waited on for long, since
// the friend service answers on the game's own thread.
namespace Service::Friend::Grid0 {

struct Friend {
    // The NSA id: the id NPLN names friends by. Splatoon 3 takes its friend list from NPLN and
    // then asks the local friend service about each friend by this id.
    u64 nsa_id{};
    std::string nickname;
    std::string login;
    std::string friend_code;
    // ONLINE, PLAYING, OFFLINE, or empty when the friend hides their presence.
    std::string state;
    std::string app_id;
    // The game's presence blob (nn::friends app key-value storage), as the friend reported it.
    std::vector<u8> app_field;
    std::string image_url;
    bool is_favorite{};
};

struct FriendRequest {
    std::string id;
    Friend other;
};

struct Me {
    u64 nsa_id{};
    std::string nickname;
    std::string login;
    std::string friend_code;
};

/// Whether an account is configured at all. Everything below is empty/false when not.
bool IsEnabled();

/// The cached friend list, refreshing it in the background when stale. Never blocks.
std::vector<Friend> Get();

/// The friend list, waiting up to `wait` for a first fetch. For a game that asks exactly once,
/// early, and would otherwise keep an empty list for the whole session.
std::vector<Friend> GetWarm(std::chrono::milliseconds wait);

/// Fetches the friend list now, blocking. For the Friends window, after an action.
std::optional<std::vector<Friend>> FetchNowBlocking();

/// Forgets the cache, so the next Get fetches again.
void Invalidate();

/// A friend's profile picture (JPEG), cached. Waits up to `wait` for a first download.
std::optional<std::vector<u8>> ProfileImage(u64 nsa_id, std::chrono::milliseconds wait);

/// Reports this player's presence: the nn::friends status (0 offline, 1 online, 2 online play),
/// the running title, and the game's presence blob. Rate limited and sent in the background.
void PublishPresence(u32 status, u64 title_id, const std::vector<u8>& app_field);

/// A game opened the friend service: show it as playing that title unless it sets its own
/// presence, which is what a console's friends list shows for a running game.
void NoteRunningTitle(u64 title_id);

// ---- For the Friends window: blocking calls, run them off the UI thread. ----

std::optional<Me> FetchMe();
bool FetchRequests(std::vector<FriendRequest>& incoming, std::vector<FriendRequest>& outgoing);

/// Sends a friend request by friend code. Returns an error message for the player, or nullopt.
std::optional<std::string> SendRequest(const std::string& friend_code);

/// action is "accept", "deny" or "cancel". Returns an error message, or nullopt.
std::optional<std::string> SettleRequest(const std::string& id, const std::string& action);

/// Returns an error message, or nullopt.
std::optional<std::string> RemoveFriend(u64 nsa_id);

} // namespace Service::Friend::Grid0
