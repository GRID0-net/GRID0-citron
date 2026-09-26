// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "common/common_types.h"

namespace FileSys {

/**
 * Built-in IPS32 patches an executable needs before it will talk to a private replacement for
 * Nintendo's servers, or none. Keyed on the executable's exact build id (hex, trailing zeros
 * trimmed, any case), and only returned while a private server address is set.
 *
 * Splatoon 3 is the reason this exists: its online client does TLS itself, over raw sockets, with
 * its own statically linked TLS stack and pinned certificates, so trusting a CA in the emulated
 * ssl service never reaches it and the game sits on "connecting" forever. These are the same two
 * patches a console running SwitchNet installs as exefs_patches: the certificate-pinning check
 * forced to pass, and the peer-hostname comparison that follows it. A title update changes the
 * build id and silently stops them applying.
 */
std::vector<std::span<const u8>> GetPrivateServerPatches(std::string_view build_id);

} // namespace FileSys
