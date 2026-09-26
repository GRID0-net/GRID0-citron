// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cctype>

#include "common/settings.h"
#include "core/file_sys/private_server_patches.h"

namespace FileSys {

namespace {

// 0x00157B20: LDRB W10,[X21,#0x38] (AA E2 40 39) -> MOV W10,#1 (2A 00 80 52).
// The pinned-certificate check always passes.
constexpr std::array<u8, 19> CertificatePinningBypass{
    'I',  'P',  'S',  '3',  '2',        // magic
    0x00, 0x15, 0x7B, 0x20, 0x00, 0x04, // offset, size
    0x2A, 0x00, 0x80, 0x52,             //
    'E',  'E',  'O',  'F',              // end
};

// 0x0014E1B0: CBZ W0,+88 (C0 02 00 34) -> NOP (1F 20 03 D5), and
// 0x0014DD80: MOV W20,W0 (F4 03 00 2A) -> MOV W20,WZR (F4 03 1F 2A).
// The peer-hostname comparison stops refusing a certificate whose name is not Nintendo's own.
constexpr std::array<u8, 29> PeerHostnameFix{
    'I',  'P',  'S',  '3',  '2',        //
    0x00, 0x14, 0xE1, 0xB0, 0x00, 0x04, //
    0x1F, 0x20, 0x03, 0xD5,             //
    0x00, 0x14, 0xDD, 0x80, 0x00, 0x04, //
    0xF4, 0x03, 0x1F, 0x2A,             //
    'E',  'E',  'O',  'F',              //
};

struct BuildPatches {
    std::string_view build_id;
    std::array<std::span<const u8>, 2> patches;
    size_t count;
};

// Splatoon 3 builds. 11.3.0's binary grew by 4096 bytes after both patch sites, so 11.2.0's bytes
// apply to it unchanged. The third, older build is covered by the certificate bypass only, as
// recorded; its peer-hostname offsets were never established.
constexpr std::array<BuildPatches, 3> Splatoon3{{
    {"6830B3A12406CB4716FEC5ADDC35D3E2DC92D212", {CertificatePinningBypass, PeerHostnameFix}, 2},
    {"28C4287AEE36F7499DA60F3E68B54C70DA382D75", {CertificatePinningBypass, PeerHostnameFix}, 2},
    {"726D2B882DD9EF10F4A9D73EED088740630FB6C8", {CertificatePinningBypass, {}}, 1},
}};

bool SameBuildId(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) ==
                      std::toupper(static_cast<unsigned char>(y));
           });
}

} // namespace

std::vector<std::span<const u8>> GetPrivateServerPatches(std::string_view build_id) {
    if (Settings::values.private_server_address.GetValue().empty()) {
        return {};
    }
    for (const auto& entry : Splatoon3) {
        if (SameBuildId(entry.build_id, build_id)) {
            return {entry.patches.begin(), entry.patches.begin() + entry.count};
        }
    }
    return {};
}

} // namespace FileSys
