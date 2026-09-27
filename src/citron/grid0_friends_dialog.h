// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QDialog>

#include "core/hle/service/friend/grid0_friends.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTimer;

/// The GRID0+ friends window: your friend code, your friends and what they are playing, adding
/// a friend by code, and answering friend requests. Everything it shows comes from the server
/// the emulator is signed in to (Configure → Network).
class Grid0FriendsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit Grid0FriendsDialog(QWidget* parent = nullptr);
    ~Grid0FriendsDialog() override;

private:
    void Refresh();
    void ShowNotSignedIn();
    void Populate(const std::optional<Service::Friend::Grid0::Me>& me,
                  const std::vector<Service::Friend::Grid0::Friend>& friends,
                  const std::vector<Service::Friend::Grid0::FriendRequest>& incoming,
                  const std::vector<Service::Friend::Grid0::FriendRequest>& outgoing);
    void AddFriend();
    void Settle(const QString& id, const QString& action);
    void Remove(quint64 nsa_id, const QString& name);
    void SetBusy(bool busy, const QString& message = {});
    /// Runs `work` off the UI thread, then `done` back on it, if the window still exists.
    void RunInBackground(std::function<void()> work, std::function<void()> done);

    QLabel* me_label;
    QPushButton* copy_code_button;
    QLineEdit* add_code;
    QPushButton* add_button;
    QTabWidget* tabs;
    QListWidget* friends_list;
    QListWidget* requests_list;
    QLabel* status_label;
    QTimer* refresh_timer;
    QString my_friend_code;
    bool refreshing = false;
};
