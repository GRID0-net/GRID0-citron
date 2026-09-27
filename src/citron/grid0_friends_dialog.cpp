// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <map>
#include <memory>
#include <thread>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include "core/hle/service/friend/grid0_friends.h"
#include "citron/grid0_friends_dialog.h"

namespace Grid0 = Service::Friend::Grid0;

namespace {

constexpr int AvatarSize = 40;

QString FormatFriendCode(const std::string& code) {
    QString digits;
    for (const char c : code) {
        if (c >= '0' && c <= '9') {
            digits += QLatin1Char(c);
        }
    }
    if (digits.size() != 12) {
        return QString::fromStdString(code);
    }
    return QStringLiteral("SW-%1-%2-%3").arg(digits.mid(0, 4), digits.mid(4, 4), digits.mid(8, 4));
}

QString GameName(const std::string& app_id) {
    if (app_id == "0100c2500fc20000") {
        return QStringLiteral("Splatoon 3");
    }
    if (app_id == "01003bc0000a0000") {
        return QStringLiteral("Splatoon 2");
    }
    return app_id.empty() ? QString{} : QObject::tr("a game (%1)").arg(QString::fromStdString(app_id));
}

QString StatusText(const Grid0::Friend& f) {
    if (f.state == "PLAYING") {
        const QString game = GameName(f.app_id);
        return game.isEmpty() ? QObject::tr("Playing") : QObject::tr("Playing %1").arg(game);
    }
    if (f.state == "ONLINE") {
        return QObject::tr("Online");
    }
    if (f.state.empty()) {
        return QObject::tr("Status hidden");
    }
    return QObject::tr("Offline");
}

QColor StatusColor(const Grid0::Friend& f) {
    if (f.state == "PLAYING") {
        return QColor(0x2e, 0xcc, 0x71);
    }
    if (f.state == "ONLINE") {
        return QColor(0x34, 0x98, 0xdb);
    }
    return QColor(0x7f, 0x8c, 0x8d);
}

// A round avatar with a status dot, or the initial when there is no picture yet.
QIcon Avatar(const Grid0::Friend& f, const std::optional<std::vector<u8>>& jpeg) {
    QPixmap canvas(AvatarSize, AvatarSize);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath circle;
    circle.addEllipse(0, 0, AvatarSize, AvatarSize);
    p.setClipPath(circle);
    QPixmap picture;
    if (jpeg && picture.loadFromData(jpeg->data(), static_cast<uint>(jpeg->size()))) {
        p.drawPixmap(0, 0, AvatarSize, AvatarSize, picture);
    } else {
        p.fillRect(0, 0, AvatarSize, AvatarSize, QColor(0x44, 0x44, 0x55));
        p.setPen(Qt::white);
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(AvatarSize / 2);
        p.setFont(font);
        const QString name = QString::fromStdString(f.nickname);
        p.drawText(QRect(0, 0, AvatarSize, AvatarSize), Qt::AlignCenter,
                   name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());
    }
    p.setClipping(false);
    const int dot = AvatarSize / 3;
    p.setPen(QPen(QColor(0x20, 0x20, 0x28), 2));
    p.setBrush(StatusColor(f));
    p.drawEllipse(AvatarSize - dot - 1, AvatarSize - dot - 1, dot, dot);
    return QIcon(canvas);
}

int Rank(const Grid0::Friend& f) {
    return f.state == "PLAYING" ? 0 : f.state == "ONLINE" ? 1 : 2;
}

} // namespace

Grid0FriendsDialog::Grid0FriendsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("GRID0+ Friends"));
    setMinimumSize(420, 520);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);

    auto* header = new QHBoxLayout;
    me_label = new QLabel(tr("Signing in…"));
    me_label->setTextFormat(Qt::RichText);
    copy_code_button = new QPushButton(tr("Copy friend code"));
    copy_code_button->setEnabled(false);
    header->addWidget(me_label, 1);
    header->addWidget(copy_code_button);
    layout->addLayout(header);

    auto* add_row = new QHBoxLayout;
    add_code = new QLineEdit;
    add_code->setPlaceholderText(tr("Friend code, e.g. SW-1234-5678-9012"));
    add_button = new QPushButton(tr("Add friend"));
    add_row->addWidget(add_code, 1);
    add_row->addWidget(add_button);
    layout->addLayout(add_row);

    tabs = new QTabWidget;
    friends_list = new QListWidget;
    friends_list->setIconSize(QSize(AvatarSize, AvatarSize));
    friends_list->setSpacing(2);
    friends_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    requests_list = new QListWidget;
    requests_list->setSpacing(2);
    tabs->addTab(friends_list, tr("Friends"));
    tabs->addTab(requests_list, tr("Requests"));
    layout->addWidget(tabs, 1);

    auto* footer = new QHBoxLayout;
    status_label = new QLabel;
    status_label->setStyleSheet(QStringLiteral("color: gray;"));
    auto* refresh_button = new QPushButton(tr("Refresh"));
    footer->addWidget(status_label, 1);
    footer->addWidget(refresh_button);
    layout->addLayout(footer);

    auto* remove_action = new QAction(tr("Remove friend"), friends_list);
    friends_list->addAction(remove_action);
    connect(remove_action, &QAction::triggered, this, [this] {
        if (auto* item = friends_list->currentItem()) {
            Remove(item->data(Qt::UserRole).toULongLong(), item->data(Qt::UserRole + 1).toString());
        }
    });
    connect(copy_code_button, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(my_friend_code);
        status_label->setText(tr("Friend code copied."));
    });
    connect(add_button, &QPushButton::clicked, this, &Grid0FriendsDialog::AddFriend);
    connect(add_code, &QLineEdit::returnPressed, this, &Grid0FriendsDialog::AddFriend);
    connect(refresh_button, &QPushButton::clicked, this, &Grid0FriendsDialog::Refresh);

    refresh_timer = new QTimer(this);
    refresh_timer->setInterval(15000);
    connect(refresh_timer, &QTimer::timeout, this, &Grid0FriendsDialog::Refresh);
    refresh_timer->start();

    if (!Grid0::IsEnabled()) {
        ShowNotSignedIn();
        return;
    }
    Refresh();
}

Grid0FriendsDialog::~Grid0FriendsDialog() = default;

void Grid0FriendsDialog::RunInBackground(std::function<void()> work, std::function<void()> done) {
    QPointer<Grid0FriendsDialog> self(this);
    std::thread([self, background = std::move(work), finish = std::move(done)] {
        background();
        QMetaObject::invokeMethod(
            qApp,
            [self, finish] {
                if (self) {
                    finish();
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

void Grid0FriendsDialog::ShowNotSignedIn() {
    me_label->setText(tr("<b>Not signed in.</b><br>Get a login from the GRID0+ Discord bot "
                         "(<code>/register</code>), then enter it in Configure → Network."));
    add_button->setEnabled(false);
    add_code->setEnabled(false);
}

void Grid0FriendsDialog::SetBusy(bool busy, const QString& message) {
    add_button->setEnabled(!busy && Grid0::IsEnabled());
    if (!message.isEmpty()) {
        status_label->setText(message);
    }
}

void Grid0FriendsDialog::Refresh() {
    if (refreshing || !Grid0::IsEnabled()) {
        return;
    }
    refreshing = true;
    auto me = std::make_shared<std::optional<Grid0::Me>>();
    auto friends = std::make_shared<std::vector<Grid0::Friend>>();
    auto incoming = std::make_shared<std::vector<Grid0::FriendRequest>>();
    auto outgoing = std::make_shared<std::vector<Grid0::FriendRequest>>();
    auto pictures = std::make_shared<std::map<u64, std::optional<std::vector<u8>>>>();
    RunInBackground(
        [=] {
            *me = Grid0::FetchMe();
            if (!*me) {
                return;
            }
            Grid0::Invalidate();
            *friends = Grid0::GetWarm(std::chrono::milliseconds(5000));
            for (const auto& f : *friends) {
                (*pictures)[f.nsa_id] = Grid0::ProfileImage(f.nsa_id, std::chrono::milliseconds(3000));
            }
            Grid0::FetchRequests(*incoming, *outgoing);
        },
        [=, this] {
            refreshing = false;
            if (!*me) {
                me_label->setText(tr("<b>Could not sign in to GRID0+.</b><br>Check the login in "
                                     "Configure → Network, and that the server is reachable."));
                return;
            }
            Populate(*me, *friends, *incoming, *outgoing);
            for (int i = 0; i < friends_list->count(); ++i) {
                auto* item = friends_list->item(i);
                const u64 id = item->data(Qt::UserRole).toULongLong();
                for (const auto& f : *friends) {
                    if (f.nsa_id == id) {
                        item->setIcon(Avatar(f, (*pictures)[id]));
                    }
                }
            }
        });
}

void Grid0FriendsDialog::Populate(const std::optional<Grid0::Me>& me,
                                  const std::vector<Grid0::Friend>& friends_in,
                                  const std::vector<Grid0::FriendRequest>& incoming,
                                  const std::vector<Grid0::FriendRequest>& outgoing) {
    my_friend_code = FormatFriendCode(me->friend_code);
    me_label->setText(tr("<b>%1</b><br>Friend code: <b>%2</b>")
                          .arg(QString::fromStdString(me->nickname).toHtmlEscaped(), my_friend_code));
    copy_code_button->setEnabled(true);

    auto friends = friends_in;
    std::stable_sort(friends.begin(), friends.end(),
                     [](const auto& a, const auto& b) { return Rank(a) < Rank(b); });
    friends_list->clear();
    int online = 0;
    for (const auto& f : friends) {
        online += f.state == "ONLINE" || f.state == "PLAYING";
        const QString name = QString::fromStdString(f.nickname);
        auto* item = new QListWidgetItem(Avatar(f, std::nullopt),
                                         QStringLiteral("%1\n%2").arg(name, StatusText(f)));
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(f.nsa_id));
        item->setData(Qt::UserRole + 1, name);
        item->setToolTip(tr("Friend code %1 · right-click to remove").arg(FormatFriendCode(f.friend_code)));
        friends_list->addItem(item);
    }
    if (friends.empty()) {
        auto* item = new QListWidgetItem(tr("No friends yet. Add one with their friend code above."));
        item->setFlags(Qt::NoItemFlags);
        friends_list->addItem(item);
    }

    requests_list->clear();
    const auto add_request = [this](const Grid0::FriendRequest& r, bool is_incoming) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 4, 6, 4);
        const QString name = QString::fromStdString(r.other.nickname);
        h->addWidget(new QLabel(is_incoming ? tr("From <b>%1</b>").arg(name.toHtmlEscaped())
                                            : tr("To <b>%1</b>").arg(name.toHtmlEscaped())),
                     1);
        const QString id = QString::fromStdString(r.id);
        if (is_incoming) {
            auto* accept = new QPushButton(tr("Accept"));
            auto* decline = new QPushButton(tr("Decline"));
            connect(accept, &QPushButton::clicked, this, [this, id] { Settle(id, QStringLiteral("accept")); });
            connect(decline, &QPushButton::clicked, this, [this, id] { Settle(id, QStringLiteral("deny")); });
            h->addWidget(accept);
            h->addWidget(decline);
        } else {
            auto* cancel = new QPushButton(tr("Cancel"));
            connect(cancel, &QPushButton::clicked, this, [this, id] { Settle(id, QStringLiteral("cancel")); });
            h->addWidget(cancel);
        }
        auto* item = new QListWidgetItem;
        item->setSizeHint(row->sizeHint());
        requests_list->addItem(item);
        requests_list->setItemWidget(item, row);
    };
    for (const auto& r : incoming) {
        add_request(r, true);
    }
    for (const auto& r : outgoing) {
        add_request(r, false);
    }
    if (incoming.empty() && outgoing.empty()) {
        auto* item = new QListWidgetItem(tr("No friend requests."));
        item->setFlags(Qt::NoItemFlags);
        requests_list->addItem(item);
    }
    tabs->setTabText(1, incoming.empty() ? tr("Requests") : tr("Requests (%1)").arg(incoming.size()));
    tabs->setTabText(0, tr("Friends (%1 online)").arg(online));
    status_label->setText(tr("Updated %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
}

void Grid0FriendsDialog::AddFriend() {
    const std::string code = add_code->text().trimmed().toStdString();
    if (code.empty()) {
        return;
    }
    SetBusy(true, tr("Sending friend request…"));
    auto problem = std::make_shared<std::optional<std::string>>();
    RunInBackground([=] { *problem = Grid0::SendRequest(code); },
                    [=, this] {
                        SetBusy(false);
                        if (*problem) {
                            QMessageBox::warning(this, tr("Add friend"), QString::fromStdString(**problem));
                            status_label->clear();
                            return;
                        }
                        add_code->clear();
                        status_label->setText(tr("Friend request sent."));
                        Refresh();
                    });
}

void Grid0FriendsDialog::Settle(const QString& id, const QString& action) {
    SetBusy(true, tr("Working…"));
    auto problem = std::make_shared<std::optional<std::string>>();
    RunInBackground([=] { *problem = Grid0::SettleRequest(id.toStdString(), action.toStdString()); },
                    [=, this] {
                        SetBusy(false);
                        if (*problem) {
                            QMessageBox::warning(this, tr("Friend request"), QString::fromStdString(**problem));
                        }
                        Refresh();
                    });
}

void Grid0FriendsDialog::Remove(quint64 nsa_id, const QString& name) {
    if (QMessageBox::question(this, tr("Remove friend"), tr("Remove %1 from your friends?").arg(name)) !=
        QMessageBox::Yes) {
        return;
    }
    auto problem = std::make_shared<std::optional<std::string>>();
    RunInBackground([=] { *problem = Grid0::RemoveFriend(nsa_id); },
                    [=, this] {
                        if (*problem) {
                            QMessageBox::warning(this, tr("Remove friend"), QString::fromStdString(**problem));
                        }
                        Refresh();
                    });
}
