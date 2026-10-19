// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuzu/eden_transfer.h"
#include <algorithm>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>

namespace EdenTransfer {
namespace {
QString Absolute(const QString& path) {
    QFileInfo info(path);
    return QDir::cleanPath(info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
}
bool Within(const QString& path, const QString& root) {
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return path.compare(root, cs) == 0 || path.startsWith(prefix, cs);
}
struct File { QString source; QString target; QString backup; };
bool AtomicCopy(const QString& from, const QString& to, const std::atomic_bool* cancelled = nullptr) {
    if (cancelled && cancelled->load()) return false;
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly) || !QDir().mkpath(QFileInfo(to).absolutePath())) return false;
    QSaveFile out(to);
    if (!out.open(QIODevice::WriteOnly)) return false;
    while (!in.atEnd()) {
        if (cancelled && cancelled->load()) return false;
        const auto block = in.read(1024 * 1024);
        if (block.isEmpty() && in.error() != QFileDevice::NoError) return false;
        if (out.write(block) != block.size()) return false;
    }
    return out.commit();
}
}
Source FromDirectory(const QString& directory) {
    QString data = Absolute(directory);
    if (QFileInfo::exists(data + QStringLiteral("/user/config/qt-config.ini"))) data += QStringLiteral("/user");
    QString config = data + QStringLiteral("/config");
#ifndef Q_OS_WIN
    if (QFileInfo(data).fileName() == QStringLiteral("eden") && QFileInfo(QFileInfo(data).absolutePath()).fileName() == QStringLiteral("share")) {
        const QString home = QDir(data + QStringLiteral("/../../..")).absolutePath();
        if (QFileInfo::exists(home + QStringLiteral("/.config/eden/qt-config.ini"))) config = home + QStringLiteral("/.config/eden");
    }
#endif
    if (QFileInfo::exists(data + QStringLiteral("/qt-config.ini"))) {
        config = data;
        if (QFileInfo(data).fileName() == QStringLiteral("config")) data = QFileInfo(data).absolutePath();
#ifndef Q_OS_WIN
        else if (QFileInfo(QFileInfo(data).absolutePath()).fileName() == QStringLiteral(".config"))
            data = QDir(config + QStringLiteral("/../../.local/share/eden")).absolutePath();
#endif
    }
#ifndef Q_OS_WIN
    const QString standard_config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/eden");
    if (Absolute(config) == Absolute(standard_config))
        data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/eden");
#endif
    return {data, config};
}
QList<Source> Detect() {
    QList<Source> result;
    auto add = [&](Source s) {
        if (!QFileInfo::exists(s.config + QStringLiteral("/qt-config.ini"))) return;
        for (const auto& old : result) if (Absolute(old.config) == Absolute(s.config)) return;
        result.push_back(s);
    };
#ifdef Q_OS_WIN
    add(FromDirectory(qEnvironmentVariable("APPDATA") + QStringLiteral("/eden")));
#else
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/eden");
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/eden");
    add({data, config});
    add(FromDirectory(data));
#endif
    const QString app = QCoreApplication::applicationDirPath();
    add(FromDirectory(app + QStringLiteral("/../Eden/user")));
    add(FromDirectory(app + QStringLiteral("/../eden/user")));
    add(FromDirectory(QDir::homePath() + QStringLiteral("/Eden/user")));
    return result;
}
Result Copy(const Source& original, const QString& data_path, const QString& config_path, bool full, const std::atomic_bool* cancelled) {
    Result result;
    if (cancelled && cancelled->load()) { result.error = QStringLiteral("Transfer cancelled."); return result; }
    const Source source{Absolute(original.data), Absolute(original.config)};
    const QString data = Absolute(data_path), config = Absolute(config_path);
    auto fail = [&](const QString& message) { result.error = message; return result; };
    if (!QFileInfo::exists(source.config + QStringLiteral("/qt-config.ini")))
        return fail(QStringLiteral("The selected folder does not contain Eden's config/qt-config.ini or qt-config.ini."));
    for (const auto& a : {source.data, source.config})
        for (const auto& b : {data, config})
            if (Within(a, b) || Within(b, a)) return fail(QStringLiteral("The Eden and Citrosis folders must be separate."));
    QMap<QString, QString> mappings;
    const QStringList folders{QStringLiteral("nand"), QStringLiteral("sdmc"), QStringLiteral("keys"), QStringLiteral("load"), QStringLiteral("amiibo"), QStringLiteral("tas"), QStringLiteral("screenshots"), QStringLiteral("dump"), QStringLiteral("saves"), QStringLiteral("play_time"), QStringLiteral("state"), QStringLiteral("icons"),
        QStringLiteral("lossless"), QStringLiteral("post_shaders"), QStringLiteral("post_presets"), QStringLiteral("grid0plus")};
    for (const auto& folder : folders) mappings[Absolute(source.data + QLatin1Char('/') + folder)] = data + QLatin1Char('/') + folder;
    QSettings original_config(source.config + QStringLiteral("/qt-config.ini"), QSettings::IniFormat);
    auto nand_source = original_config.value(QStringLiteral("Data Storage/nand_directory")).toString();
    if (nand_source.isEmpty()) nand_source = source.data + QStringLiteral("/nand");
    if (QDir::isRelativePath(nand_source)) nand_source = QDir(source.data).absoluteFilePath(nand_source);
    const QMap<QString, QString> storage{{QStringLiteral("nand_directory"), QStringLiteral("nand")}, {QStringLiteral("sdmc_directory"), QStringLiteral("sdmc")},
        {QStringLiteral("save_directory"), QStringLiteral("nand")}, {QStringLiteral("load_directory"), QStringLiteral("load")}, {QStringLiteral("dump_directory"), QStringLiteral("dump")},
        {QStringLiteral("tas_directory"), QStringLiteral("tas")}, {QStringLiteral("screenshot_path"), QStringLiteral("screenshots")}};
    for (const auto& key : original_config.allKeys()) {
        auto found = storage.find(key.section(QLatin1Char('/'), -1));
        if (found == storage.end()) continue;
        auto value = original_config.value(key).toString();
        if (value.isEmpty()) value = found.key() == QStringLiteral("save_directory") ? nand_source : source.data + QLatin1Char('/') + found.value();
        if (QDir::isRelativePath(value)) value = QDir(source.data).absoluteFilePath(value);
        value = Absolute(value);
        if (full && found.key() != QStringLiteral("save_directory")) {
            const QString target = data + QLatin1Char('/') + found.value();
            for (auto it = mappings.begin(); it != mappings.end();) {
                if (it.value() == target) it = mappings.erase(it); else ++it;
            }
            mappings[value] = target;
        } else if (full && std::none_of(mappings.keyBegin(), mappings.keyEnd(), [&](const QString& root) { return Within(value, root); })) {
            mappings[value] = data + QStringLiteral("/saves");
        }
    }
    if (full) {
        auto ca = original_config.value(QStringLiteral("Services/private_server_ca_bundle")).toString();
        if (!ca.isEmpty()) {
            if (QDir::isRelativePath(ca)) ca = QDir(source.data).absoluteFilePath(ca);
            if (QFileInfo(ca).isFile()) mappings[Absolute(ca)] = data + QStringLiteral("/keys/grid0-ca.pem");
        }
    }
    QMap<QString, QString> plan;
    auto addTree = [&](const QString& from, const QString& to) -> bool {
        if (!QFileInfo::exists(from)) return true;
        if (QFileInfo(from).isSymLink()) return false;
        for (const auto& destination : {data, config})
            if (Within(Absolute(from), destination) || Within(destination, Absolute(from))) return false;
        if (QFileInfo(from).isFile()) { plan[to] = from; return true; }
        QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (cancelled && cancelled->load()) return false;
            if (it.fileInfo().isSymLink()) return false;
            if (!it.fileInfo().isFile()) continue;
            if (it.fileName() == QStringLiteral("lsfg_spirv.cache") && QFileInfo(from).fileName() == QStringLiteral("lossless")) continue;
            const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(it.filePath());
            if (plan.contains(target) && Absolute(plan[target]) != Absolute(it.filePath())) return false;
            plan[target] = it.filePath();
        }
        return true;
    };
    if (!addTree(source.config, config)) return fail(cancelled && cancelled->load() ? QStringLiteral("Transfer cancelled.") : QStringLiteral("A configuration path overlaps Citrosis or contains a symbolic link."));
    if (full) for (auto it = mappings.begin(); it != mappings.end(); ++it)
        if (!addTree(it.key(), it.value())) return fail(cancelled && cancelled->load() ? QStringLiteral("Transfer cancelled.") : QStringLiteral("A data path overlaps Citrosis, conflicts with another path, or contains a symbolic link. Copy that data manually first."));
    result.backup = data + QStringLiteral("/transfer-backups/") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128);
    if (QFileInfo(data + QStringLiteral("/transfer-backups")).isSymLink())
        return fail(QStringLiteral("The backup directory must not be a symbolic link."));
    QList<File> files;
    qint64 required = 0;
    for (auto it = plan.begin(); it != plan.end(); ++it) {
        const QString target = it.key();
        QString ancestor = target;
        while (ancestor != data && ancestor != config && ancestor != QFileInfo(ancestor).absolutePath()) {
            if (QFileInfo(ancestor).isSymLink()) return fail(QStringLiteral("A destination path contains a symbolic link."));
            ancestor = QFileInfo(ancestor).absolutePath();
        }
        if (QFileInfo(target).exists() && !QFileInfo(target).isFile()) return fail(QStringLiteral("A destination file is a directory: ") + target);
        const QString relative = Within(target, config) ? QStringLiteral("config/") + QDir(config).relativeFilePath(target) : QStringLiteral("data/") + QDir(data).relativeFilePath(target);
        files.push_back({it.value(), target, result.backup + QLatin1Char('/') + relative});
        required += QFileInfo(it.value()).size() + QFileInfo(target).size();
    }
    QStorageInfo disk(data);
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < required)
        return fail(QStringLiteral("There is not enough free space for the transfer and backup."));
    // Finish every backup before changing a destination file.
    for (const auto& file : files) if (QFileInfo::exists(file.target) && !AtomicCopy(file.target, file.backup, cancelled))
        return fail(cancelled && cancelled->load() ? QStringLiteral("Transfer cancelled.") : QStringLiteral("Could not back up ") + file.target);
    QList<File> written;
    auto rollback = [&](const QString& message) {
        bool restored = true;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            if (QFileInfo::exists(it->backup)) restored &= AtomicCopy(it->backup, it->target);
            else restored &= QFile::remove(it->target);
        }
        return fail(message + (restored ? QStringLiteral("\nOriginal files restored.") : QStringLiteral("\nSome files could not be restored. Backup: ") + result.backup));
    };
    for (const auto& file : files) {
        if (!AtomicCopy(file.source, file.target, cancelled)) return rollback(cancelled && cancelled->load() ? QStringLiteral("Transfer cancelled.") : QStringLiteral("Could not copy ") + file.source);
        written.push_back(file);
        if (Within(file.target, config) && file.target.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive)) {
            bool ini_ok = false;
            {
            QSettings settings(file.target, QSettings::IniFormat);
            if (QFileInfo(file.target).fileName() == QStringLiteral("qt-config.ini")) {
                for (auto it = storage.begin(); it != storage.end(); ++it) {
                    const QString key = (it.key() == QStringLiteral("screenshot_path") ? QStringLiteral("UI/Screenshots/") : QStringLiteral("Data Storage/")) + it.key();
                    if (!settings.contains(key)) settings.setValue(key, it.key() == QStringLiteral("save_directory") ? nand_source : source.data + QLatin1Char('/') + it.value());
                }
            }
            for (const auto& key : settings.allKeys()) {
                const auto name = key.section(QLatin1Char('/'), -1);
                if (!storage.contains(name)) {
                    if (full && !key.startsWith(QStringLiteral("UI/Paths/")) &&
                        (name.endsWith(QStringLiteral("_path")) || name == QStringLiteral("private_server_ca_bundle"))) {
                        auto value = settings.value(key).toString();
                        if (!value.isEmpty()) {
                            if (QDir::isRelativePath(value)) value = QDir(source.data).absoluteFilePath(value);
                            value = Absolute(value);
                            QString best;
                            for (auto it = mappings.begin(); it != mappings.end(); ++it)
                                if (Within(value, it.key()) && it.key().size() > best.size()) best = it.key();
                            if (!best.isEmpty()) settings.setValue(key, QString(mappings[best] + value.mid(best.size())));
                        }
                    }
                    continue;
                }
                auto value = settings.value(key).toString();
                if (value.isEmpty()) value = name == QStringLiteral("save_directory") ? nand_source : source.data + QLatin1Char('/') + storage[name];
                if (QDir::isRelativePath(value)) value = QDir(source.data).absoluteFilePath(value);
                value = Absolute(value);
                if (full) {
                    QString best;
                    for (auto it = mappings.begin(); it != mappings.end(); ++it)
                        if (Within(value, it.key()) && it.key().size() > best.size()) best = it.key();
                    if (!best.isEmpty()) value = mappings[best] + value.mid(best.size());
                }
                settings.setValue(key, value);
                settings.setValue(key + QStringLiteral("/default"), false);
            }
            settings.sync();
            ini_ok = settings.status() == QSettings::NoError;
            }
            if (!ini_ok) return rollback(QStringLiteral("Could not update imported paths."));
        }
    }
    if (cancelled && cancelled->load()) return rollback(QStringLiteral("Transfer cancelled."));
    result.ok = true;
    result.files = files.size();
    return result;
}
}
