// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "common/common_types.h"

namespace FileSys {

/**
 * IPS patches an executable needs before it will talk to a private replacement for Nintendo's
 * servers, or none. Keyed on the executable's exact build id (hex, trailing zeros trimmed, any
 * case), and only returned while a private server address is set.
 *
 * Splatoon 3 is the reason this exists: its online client does TLS itself, over raw sockets, with
 * its own statically linked TLS stack and pinned certificates, so trusting a CA in the emulated
 * ssl service never reaches it and the game sits on "connecting" forever. The patches are the real
 * .ips files the GRID0+ Toolbox installs on a console, from the newest patches-<serial> release
 * of GRID0-net/GRID0plus-cfw. None ship inside the emulator: the frontend downloads that release's
 * Splatoon 3 patches into PrivateServerPatchesDir() at startup (citron/grid0_patch_update.cpp),
 * laid out as exefs_patches/<category>/<build id>.ips like the release manifest. Its system ssl,
 * bcat and browser patches are not downloaded: they target modules this emulator emulates
 * instead of running. A title update changes the build id and silently stops them applying.
 */
std::vector<std::vector<u8>> GetPrivateServerPatches(std::string_view build_id);

/// Where the downloaded patches live. Exclusively GRID0+'s: the updater deletes anything in it the
/// newest manifest does not list.
std::filesystem::path PrivateServerPatchesDir();

} // namespace FileSys
