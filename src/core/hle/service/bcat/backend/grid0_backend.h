// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/bcat/backend/backend.h"

namespace Service::BCAT {

// A game's BCAT delivery cache from a GRID0+ server.
//
// A console's bcat sysmodule downloads it from the BCAT servers, which GRID0+ answers; Splatoon 3's
// Splatfest packs reach a console this way. The emulator has no BCAT client, so when a game asks
// for a sync this fetches the same files from GRID0+'s emulator API (/emulator/v1/bcat/<title>),
// signed in with the GRID0+ login, and keeps the title's cache directory in step with the server.
class Grid0BcatBackend final : public BcatBackend {
public:
    explicit Grid0BcatBackend(DirectoryGetter getter);
    ~Grid0BcatBackend() override;

    bool Synchronize(TitleIDVersion title, ProgressServiceBackend& progress) override;
    bool SynchronizeDirectory(TitleIDVersion title, std::string name,
                              ProgressServiceBackend& progress) override;
    bool Clear(u64 title_id) override;
    void SetPassphrase(u64 title_id, const Passphrase& passphrase) override;
    std::optional<std::vector<u8>> GetLaunchParameter(TitleIDVersion title) override;

private:
    bool Sync(TitleIDVersion title, const std::string* only_directory,
              ProgressServiceBackend& progress);
};

} // namespace Service::BCAT
