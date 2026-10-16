// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QWidget;

namespace Grid0 {

/// Keeps the GRID0+ Splatoon 3 patches (FileSys::PrivateServerPatchesDir) in step with the newest
/// patches-<serial> release of GRID0plus-cfw. If anything differs, asks the player, downloads,
/// checks every file's SHA-256 against the manifest, and restarts citron.
void CheckPatchesOnBoot(QWidget* parent);

} // namespace Grid0
