// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-2.0-or-later

// No patch ships inside citron. The newest patches-<serial> release of GRID0plus-cfw is the one
// source for the console Toolbox and both GRID0 emulators (cfw repo, patches/README.md). At every
// start the SHA-256 of each file in the patch folder is compared with the release manifest; if
// anything differs the player is asked, and on yes every changed file is downloaded, checked
// against the manifest before anything is written, and citron restarts: patches are applied when
// a game boots, so a game already running would otherwise keep the old ones. The folder is
// exclusively GRID0+'s, so files the manifest does not list are deleted.
//
// Files are fetched one by one, by hash, rather than as the release's patches.zip: the Splatoon 3
// set is a handful of small files, and citron has no zip reader on every platform.

#include "citron/grid0_patch_update.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <vector>

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/file_sys/private_server_patches.h"

namespace Grid0 {
namespace {

constexpr char ReleasesApi[] =
    "https://api.github.com/repos/GRID0-net/GRID0plus-cfw/releases?per_page=30";
constexpr char DownloadBase[] = "https://github.com/GRID0-net/GRID0plus-cfw/releases/download/";

struct Entry {
    QString dest;
    QString sha256;
    QString asset;
};

struct Run {
    QPointer<QWidget> owner;
    QNetworkAccessManager* network;
    QString tag;
    std::vector<Entry> needed;
    std::vector<std::filesystem::path> stale;
    std::vector<std::pair<Entry, QByteArray>> downloaded;
};

std::filesystem::path Root() {
    return FileSys::PrivateServerPatchesDir();
}

std::filesystem::path PathOf(const QString& dest) {
    return Root() / std::filesystem::path(dest.toStdU16String());
}

QString Sha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool Matches(const Entry& e) {
    QFile f(QString::fromStdU16String(PathOf(e.dest).u16string()));
    return f.open(QIODevice::ReadOnly) && Sha256(f.readAll()) == e.sha256;
}

QNetworkReply* Get(Run* run, const QString& url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GRID0-citron"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    return run->network->get(request);
}

void Fail(std::shared_ptr<Run> run, const QString& why) {
    LOG_WARNING(Frontend, "GRID0 patch check failed: {}", why.toStdString());
    std::error_code ec;
    if (run->owner && !std::filesystem::is_directory(Root() / "exefs_patches", ec)) {
        QMessageBox::warning(run->owner, QObject::tr("GRID0+ patches"),
                             QObject::tr("The GRID0+ patches could not be downloaded. Splatoon 3 "
                                         "cannot connect to GRID0+ without them; citron tries "
                                         "again every time it starts."));
    }
    run->network->deleteLater();
}

void Restart() {
    // An AppImage must be relaunched as the AppImage, not the binary it mounted.
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    const QString self = appimage.isEmpty() ? QCoreApplication::applicationFilePath() : appimage;
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) {
        args.removeFirst();
    }
    QProcess::startDetached(self, args);
    QApplication::quit();
}

void Install(std::shared_ptr<Run> run) {
    std::error_code ec;
    for (const auto& [entry, data] : run->downloaded) {
        const auto path = PathOf(entry.dest);
        std::filesystem::create_directories(path.parent_path(), ec);
        QSaveFile out(QString::fromStdU16String(path.u16string()));
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            Fail(run, QStringLiteral("could not write %1").arg(entry.dest));
            return;
        }
    }
    for (const auto& path : run->stale) {
        std::filesystem::remove(path, ec);
    }
    // Directories emptied by a removal, deepest first.
    std::vector<std::filesystem::path> dirs;
    for (const auto& d : std::filesystem::recursive_directory_iterator(Root(), ec)) {
        if (d.is_directory()) {
            dirs.push_back(d.path());
        }
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
    for (const auto& d : dirs) {
        if (std::filesystem::is_empty(d, ec)) {
            std::filesystem::remove(d, ec);
        }
    }
    LOG_INFO(Frontend, "GRID0 patches updated to {}; restarting", run->tag.toStdString());
    run->network->deleteLater();
    Restart();
}

// Downloads the needed files one after another; nothing is written until all have verified.
void DownloadNext(std::shared_ptr<Run> run) {
    const size_t index = run->downloaded.size();
    if (index == run->needed.size()) {
        Install(run);
        return;
    }
    const Entry entry = run->needed[index];
    QNetworkReply* reply = Get(run.get(), QString::fromLatin1(DownloadBase) + run->tag +
                                              QLatin1Char('/') + entry.asset);
    QObject::connect(reply, &QNetworkReply::finished, run->network, [run, reply, entry] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Fail(run, reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        if (Sha256(data) != entry.sha256) {
            Fail(run, QStringLiteral("%1 does not match its manifest hash").arg(entry.asset));
            return;
        }
        run->downloaded.emplace_back(entry, data);
        DownloadNext(run);
    });
}

void Compare(std::shared_ptr<Run> run, const QByteArray& manifest) {
    static const QRegularExpression safe_dest(
        QStringLiteral("^exefs_patches/[A-Za-z0-9_.-]+/[A-Za-z0-9]+\\.ips$"));
    std::vector<Entry> entries;
    for (const QJsonValue& value :
         QJsonDocument::fromJson(manifest).object().value(QStringLiteral("patches")).toArray()) {
        const QJsonObject p = value.toObject();
        if (!p.value(QStringLiteral("targets")).toArray().contains(QStringLiteral("emulator"))) {
            continue;
        }
        Entry e{p.value(QStringLiteral("dest")).toString(), p.value(QStringLiteral("sha256")).toString(),
                p.value(QStringLiteral("asset")).toString()};
        // Any malformed entry rejects the whole manifest.
        if (!safe_dest.match(e.dest).hasMatch() || e.sha256.size() != 64 ||
            e.asset != e.sha256 + QStringLiteral(".ips")) {
            Fail(run, QStringLiteral("malformed manifest entry %1").arg(e.dest));
            return;
        }
        entries.push_back(e);
    }
    if (entries.empty()) {
        Fail(run, QStringLiteral("manifest lists no emulator patches"));
        return;
    }

    std::set<std::filesystem::path> listed;
    for (const auto& e : entries) {
        listed.insert(PathOf(e.dest).lexically_normal());
        if (!Matches(e)) {
            run->needed.push_back(e);
        }
    }
    std::error_code ec;
    for (const auto& f : std::filesystem::recursive_directory_iterator(Root(), ec)) {
        if (f.is_regular_file() && !listed.contains(f.path().lexically_normal())) {
            run->stale.push_back(f.path());
        }
    }

    if (run->needed.empty() && run->stale.empty()) {
        LOG_INFO(Frontend, "GRID0 patches current ({})", run->tag.toStdString());
        run->network->deleteLater();
        return;
    }
    if (!run->owner) {
        run->network->deleteLater();
        return;
    }
    const auto changes = static_cast<int>(run->needed.size() + run->stale.size());
    const auto answer = QMessageBox::question(
        run->owner, QObject::tr("GRID0+ patch update"),
        QObject::tr("%n GRID0+ patch file(s) changed (%1).\n\nDownload now? citron restarts "
                    "afterwards so the game uses them.",
                    nullptr, changes)
            .arg(run->tag));
    if (answer != QMessageBox::Yes) {
        run->network->deleteLater();
        return;
    }
    DownloadNext(run);
}

} // namespace

void CheckPatchesOnBoot(QWidget* parent) {
    auto run = std::make_shared<Run>();
    run->owner = parent;
    run->network = new QNetworkAccessManager(parent);

    QNetworkReply* list = Get(run.get(), QString::fromLatin1(ReleasesApi));
    QObject::connect(list, &QNetworkReply::finished, run->network, [run, list] {
        list->deleteLater();
        if (list->error() != QNetworkReply::NoError) {
            Fail(run, list->errorString());
            return;
        }
        static const QRegularExpression tag_re(QStringLiteral("^patches-([0-9]+)$"));
        int best = -1;
        for (const QJsonValue& value : QJsonDocument::fromJson(list->readAll()).array()) {
            const QJsonObject release = value.toObject();
            const auto m = tag_re.match(release.value(QStringLiteral("tag_name")).toString());
            if (!release.value(QStringLiteral("draft")).toBool() && m.hasMatch() &&
                m.captured(1).toInt() > best) {
                best = m.captured(1).toInt();
                run->tag = m.captured(0);
            }
        }
        if (best < 0) {
            Fail(run, QStringLiteral("no patches release found"));
            return;
        }
        QNetworkReply* manifest =
            Get(run.get(), QString::fromLatin1(DownloadBase) + run->tag + QStringLiteral("/manifest.json"));
        QObject::connect(manifest, &QNetworkReply::finished, run->network, [run, manifest] {
            manifest->deleteLater();
            if (manifest->error() != QNetworkReply::NoError) {
                Fail(run, manifest->errorString());
                return;
            }
            Compare(run, manifest->readAll());
        });
    });
}

} // namespace Grid0
