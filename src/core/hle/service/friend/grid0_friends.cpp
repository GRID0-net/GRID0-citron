// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/logging.h"
#include "core/hle/service/acc/switchnet_account.h"
#include "core/hle/service/friend/grid0_friends.h"

namespace Service::Friend::Grid0 {

namespace Account = Service::Account::SwitchNet;

namespace {

// A friend coming online shows up this quickly; the server itself only drops a presence after
// several minutes without a refresh.
constexpr auto RefreshInterval = std::chrono::seconds(20);
// Presence is re-sent at least this often while nothing changes, to keep it from expiring.
constexpr auto PresenceKeepAlive = std::chrono::seconds(60);
// ...and never more often than this, however often the game updates it.
constexpr auto PresenceMinInterval = std::chrono::seconds(3);

std::mutex mutex;
std::condition_variable fetched_cv;
std::vector<Friend> cache;
bool have_list = false;
// Counts finished fetches, successful or not: a warm wait ends when one finishes, so a server
// that is down costs one bounded wait, not one per call.
u64 fetches_done = 0;
bool warm_waited = false;
std::chrono::steady_clock::time_point last_fetch;
std::atomic<bool> fetching{false};

std::map<u64, std::vector<u8>> images;
std::map<u64, bool> image_pending;


std::vector<u8> DecodeBase64(std::string_view in) {
    std::vector<u8> out;
    u32 buffer = 0;
    int bits = 0;
    for (const char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') {
            v = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            v = c - '0' + 52;
        } else if (c == '+' || c == '-') {
            v = 62;
        } else if (c == '/' || c == '_') {
            v = 63;
        } else {
            continue;
        }
        buffer = (buffer << 6) | static_cast<u32>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<u8>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string EncodeBase64(const std::vector<u8>& in) {
    static constexpr char Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const u32 n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += Table[(n >> 18) & 63];
        out += Table[(n >> 12) & 63];
        out += Table[(n >> 6) & 63];
        out += Table[n & 63];
    }
    if (i < in.size()) {
        const u32 n = (in[i] << 16) | (i + 1 < in.size() ? in[i + 1] << 8 : 0);
        out += Table[(n >> 18) & 63];
        out += Table[(n >> 12) & 63];
        out += i + 1 < in.size() ? Table[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

u64 ParseNsa(const std::string& hex) {
    try {
        return std::stoull(hex, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

Friend ParseFriend(const nlohmann::json& j) {
    Friend f;
    f.nsa_id = ParseNsa(j.value("id", ""));
    f.nickname = j.value("nickname", "");
    f.login = j.value("login", "");
    f.friend_code = j.value("friendCode", "");
    f.state = j.value("state", "");
    f.app_id = j.value("appId", "");
    f.image_url = j.value("imageUrl", "");
    f.is_favorite = j.value("isFavorite", false);
    const std::string field = j.value("appField", "");
    if (!field.empty()) {
        f.app_field = DecodeBase64(field);
    }
    if (f.nickname.empty()) {
        f.nickname = f.login;
    }
    return f;
}

std::optional<nlohmann::json> GetJson(const std::string& path) {
    const auto response = Account::Request("GET", path);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    auto body = nlohmann::json::parse(response->body, nullptr, false);
    if (body.is_discarded()) {
        return std::nullopt;
    }
    return body;
}

// A refusal's message for the player, or a generic one.
std::optional<std::string> Problem(const std::optional<Account::Response>& response) {
    if (!response) {
        return "Could not reach the GRID0+ server.";
    }
    if (response->status >= 200 && response->status < 300) {
        return std::nullopt;
    }
    const auto body = nlohmann::json::parse(response->body, nullptr, false);
    if (body.is_object() && body.contains("message") && body["message"].is_string()) {
        return body["message"].get<std::string>();
    }
    return fmt::format("The server refused ({}).", response->status);
}

void FetchNow() {
    std::vector<Friend> list;
    // A blocking fetch and a background one may overlap; both results are the server's state.
    bool ok = false;
    if (const auto body = GetJson("/emulator/v1/friends");
        body && body->contains("friends") && (*body)["friends"].is_array()) {
        for (const auto& entry : (*body)["friends"]) {
            Friend f = ParseFriend(entry);
            if (f.nsa_id != 0) {
                list.push_back(std::move(f));
            }
        }
        ok = true;
    }

    std::scoped_lock lock{mutex};
    last_fetch = std::chrono::steady_clock::now();
    ++fetches_done;
    if (ok) {
        if (!have_list || list.size() != cache.size()) {
            LOG_INFO(Service_Friend, "GRID0+: {} friend(s)", list.size());
        }
        cache = std::move(list);
        have_list = true;
    }
    fetched_cv.notify_all();
}

void RefreshInBackground() {
    if (fetching.exchange(true)) {
        return;
    }
    std::thread([] {
        FetchNow();
        fetching = false;
    }).detach();
}

} // namespace

bool IsEnabled() {
    return Account::IsConfigured();
}

std::vector<Friend> Get() {
    if (!IsEnabled()) {
        return {};
    }
    std::scoped_lock lock{mutex};
    if (!have_list || std::chrono::steady_clock::now() - last_fetch > RefreshInterval) {
        RefreshInBackground();
    }
    return cache;
}

std::vector<Friend> GetWarm(std::chrono::milliseconds wait) {
    if (!IsEnabled()) {
        return {};
    }
    std::unique_lock lock{mutex};
    const bool stale = std::chrono::steady_clock::now() - last_fetch > RefreshInterval;
    // Wait at most once per session: games that poll their friend list (NEX titles) must never
    // block on it, and one that asks once (Splatoon 3) only needs the first answer to be real.
    if (!have_list && !warm_waited) {
        warm_waited = true;
        const u64 before = fetches_done;
        RefreshInBackground();
        fetched_cv.wait_for(lock, wait, [before] { return fetches_done != before; });
    } else if (!have_list || stale) {
        RefreshInBackground();
    }
    return cache;
}

std::optional<std::vector<Friend>> FetchNowBlocking() {
    if (!IsEnabled()) {
        return std::nullopt;
    }
    FetchNow();
    std::scoped_lock lock{mutex};
    if (!have_list) {
        return std::nullopt;
    }
    return cache;
}

void Invalidate() {
    std::scoped_lock lock{mutex};
    last_fetch = {};
}

std::optional<std::vector<u8>> ProfileImage(u64 nsa_id, std::chrono::milliseconds wait) {
    std::string url;
    {
        std::unique_lock lock{mutex};
        if (const auto it = images.find(nsa_id); it != images.end()) {
            return it->second;
        }
        for (const auto& f : cache) {
            if (f.nsa_id == nsa_id) {
                url = f.image_url;
            }
        }
        if (url.empty()) {
            return std::nullopt;
        }
        if (!image_pending[nsa_id]) {
            image_pending[nsa_id] = true;
            std::thread([nsa_id, url] {
                auto jpeg = Account::Fetch(url);
                std::scoped_lock inner{mutex};
                image_pending[nsa_id] = false;
                if (jpeg) {
                    images[nsa_id] = std::vector<u8>(jpeg->begin(), jpeg->end());
                }
                fetched_cv.notify_all();
            }).detach();
        }
        fetched_cv.wait_for(lock, wait, [nsa_id] { return images.contains(nsa_id); });
        if (const auto it = images.find(nsa_id); it != images.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

namespace {
// What this player should currently be shown as. The sender thread sends it whenever it
// changes, and again every PresenceKeepAlive so the server does not expire it.
std::string desired_body;
std::string desired_key;
std::string sent_key;
bool sender_started = false;
std::condition_variable presence_cv;

void PresenceSender() {
    std::unique_lock lock{mutex};
    auto last_sent = std::chrono::steady_clock::time_point{};
    while (true) {
        presence_cv.wait_for(lock, std::chrono::seconds(5));
        const auto now = std::chrono::steady_clock::now();
        if (desired_key.empty() || now - last_sent < PresenceMinInterval) {
            continue;
        }
        if (desired_key == sent_key && now - last_sent < PresenceKeepAlive) {
            continue;
        }
        const std::string body = desired_body;
        const std::string key = desired_key;
        lock.unlock();
        const auto response = Account::Request("POST", "/emulator/v1/presence", body);
        lock.lock();
        last_sent = std::chrono::steady_clock::now();
        if (response && response->status < 300) {
            sent_key = key;
        } else {
            LOG_DEBUG(Service_Friend, "GRID0+: presence not accepted");
        }
    }
}
} // namespace

void PublishPresence(u32 status, u64 title_id, const std::vector<u8>& app_field) {
    if (!IsEnabled()) {
        return;
    }
    const char* state = status == 2 ? "PLAYING" : status == 1 ? "ONLINE" : "OFFLINE";
    const std::string app_id = title_id != 0 ? fmt::format("{:016x}", title_id) : std::string{};
    // Trailing zeroes are the unused part of the fixed-size storage, not data.
    std::vector<u8> field = app_field;
    while (!field.empty() && field.back() == 0) {
        field.pop_back();
    }
    const std::string encoded = EncodeBase64(field);
    const nlohmann::json body{{"state", state}, {"appId", app_id}, {"appField", encoded}};

    std::scoped_lock lock{mutex};
    desired_key = fmt::format("{}|{}|{}", state, app_id, encoded);
    desired_body = body.dump();
    if (!sender_started) {
        sender_started = true;
        std::thread(PresenceSender).detach();
    }
    presence_cv.notify_all();
}

void NoteRunningTitle(u64 title_id) {
    if (!IsEnabled() || title_id == 0) {
        return;
    }
    {
        std::scoped_lock lock{mutex};
        // A game that sets its own presence keeps it; this only covers one that never does, so
        // friends still see what it is playing.
        if (!desired_key.empty()) {
            return;
        }
    }
    PublishPresence(2, title_id, {});
}

std::optional<Me> FetchMe() {
    const auto body = GetJson("/emulator/v1/me");
    if (!body) {
        return std::nullopt;
    }
    const Friend f = ParseFriend(*body);
    return Me{f.nsa_id, f.nickname, f.login, f.friend_code};
}

bool FetchRequests(std::vector<FriendRequest>& incoming, std::vector<FriendRequest>& outgoing) {
    const auto body = GetJson("/emulator/v1/friend-requests");
    if (!body) {
        return false;
    }
    const auto read = [&](const char* key, std::vector<FriendRequest>& out) {
        out.clear();
        if (body->contains(key) && (*body)[key].is_array()) {
            for (const auto& entry : (*body)[key]) {
                out.push_back({entry.value("id", ""), ParseFriend(entry.value("other", nlohmann::json::object()))});
            }
        }
    };
    read("incoming", incoming);
    read("outgoing", outgoing);
    return true;
}

std::optional<std::string> SendRequest(const std::string& friend_code) {
    const nlohmann::json body{{"friendCode", friend_code}};
    const auto problem = Problem(Account::Request("POST", "/emulator/v1/friend-requests", body.dump()));
    Invalidate();
    return problem;
}

std::optional<std::string> SettleRequest(const std::string& id, const std::string& action) {
    const auto problem =
        Problem(Account::Request("POST", "/emulator/v1/friend-requests/" + id + "/" + action, "{}"));
    Invalidate();
    return problem;
}

std::optional<std::string> RemoveFriend(u64 nsa_id) {
    const auto problem =
        Problem(Account::Request("DELETE", fmt::format("/emulator/v1/friends/{:016x}", nsa_id)));
    Invalidate();
    return problem;
}

} // namespace Service::Friend::Grid0
