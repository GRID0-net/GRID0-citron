// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-2.0-or-later

// A check and a link, not an installer: modelled on citron-nextendo's boot-time updater,
// minus the download-and-replace half, which needs libarchive on every platform and would be
// the one piece of the emulator that runs code it fetched. The player installs by hand.

#include "citron/grid0_update_check.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVersionNumber>

#include "common/logging.h"

#ifndef GRID0_RELEASE_VERSION
#define GRID0_RELEASE_VERSION ""
#endif

namespace Grid0 {
namespace {

// The release LIST rather than /releases/latest, which skips pre-releases.
constexpr char ReleasesApi[] =
    "https://api.github.com/repos/GRID0-net/GRID0-citron/releases?per_page=10";
constexpr char ReleasesPage[] = "https://github.com/GRID0-net/GRID0-citron/releases";

QVersionNumber ParseTag(QString tag) {
    if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V'))) {
        tag.remove(0, 1);
    }
    return QVersionNumber::fromString(tag);
}

} // namespace

void CheckForUpdateOnBoot(QWidget* parent) {
    const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(GRID0_RELEASE_VERSION));
    if (current.isNull()) {
        // A local or CI build without a release version: nothing meaningful to compare.
        return;
    }

    auto* network = new QNetworkAccessManager(parent);
    QNetworkRequest request{QUrl(QString::fromLatin1(ReleasesApi))};
    // GitHub refuses requests without a User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("GRID0-citron/%1").arg(current.toString()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(10000);

    QNetworkReply* reply = network->get(request);
    QPointer<QWidget> owner{parent};

    QObject::connect(reply, &QNetworkReply::finished, network, [reply, network, owner, current] {
        reply->deleteLater();
        network->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            LOG_WARNING(Frontend, "GRID0 update check failed: {}", reply->errorString().toStdString());
            return;
        }

        QVersionNumber best;
        for (const QJsonValue& value : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject release = value.toObject();
            if (release.value(QStringLiteral("draft")).toBool()) {
                continue;
            }
            // Compare versions, not publish order: a late hotfix to an older line must not win.
            const QVersionNumber version = ParseTag(release.value(QStringLiteral("tag_name")).toString());
            if (!version.isNull() && version > best) {
                best = version;
            }
        }

        if (best.isNull() || best <= current) {
            LOG_INFO(Frontend, "GRID0 update check: up to date ({})", current.toString().toStdString());
            return;
        }
        if (!owner) {
            return;
        }

        LOG_INFO(Frontend, "GRID0 update available: {} -> {}", current.toString().toStdString(),
                 best.toString().toStdString());

        QMessageBox box(owner);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(QObject::tr("GRID0 update available"));
        box.setText(QObject::tr("A newer GRID0-citron is available: %1 → %2.")
                        .arg(current.toString(), best.toString()));
        box.setInformativeText(QObject::tr(
            "Newer builds carry the latest GRID0+ fixes and game patches. Download it from the "
            "release page and replace this one."));
        QPushButton* open = box.addButton(QObject::tr("Open download page"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Later"), QMessageBox::RejectRole);
        box.exec();

        if (box.clickedButton() == open) {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(ReleasesPage)));
        }
    });
}

} // namespace Grid0
