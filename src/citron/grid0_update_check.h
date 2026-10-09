// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QWidget;

namespace Grid0 {

/// Asks the GRID0-net release list, once, whether a newer GRID0-citron exists and, if so,
/// offers to open its download page. Silent on a dev build, when offline, or when up to date.
void CheckForUpdateOnBoot(QWidget* parent);

} // namespace Grid0
