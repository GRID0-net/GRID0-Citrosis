// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuzu/eden_transfer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <chrono>
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Write(const QString& path, QByteArray bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); Check(file.open(QIODevice::WriteOnly), "create fixture");
    Check(file.write(bytes) == bytes.size(), "write fixture");
}
QByteArray Read(const QString& path) { QFile f(path); Check(f.open(QIODevice::ReadOnly), "read fixture"); return f.readAll(); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp; Check(temp.isValid(), "temporary directory");
        const auto root = temp.path();
        const auto data = root + "/eden", config = root + "/eden-config";
        Write(data + "/nand/save/test", "save-data");
        Write(data + "/nand/save/game.ini", "[Data%20Storage]\nnand_directory=game-value\n");
        Write(data + "/keys/prod.keys", "test-only-keys");
        Write(root + "/custom-ca.pem", "test-only-public-ca");
        Write(data + "/post_shaders/custom.fx", "test-effect");
        Write(data + "/custom-sd/content", "sd-data");
        Write(root + "/external-saves/test", "external-save");
        Write(config + "/custom/game.ini", "[Renderer]\nvalue=1\n");
        {
            QSettings ini(config + "/qt-config.ini", QSettings::IniFormat);
            ini.setValue("Data Storage/nand_directory", data + "/nand");
            ini.setValue("Data Storage/nand_directory/default", true);
            ini.setValue("Data Storage/sdmc_directory", data + "/custom-sd");
            ini.setValue("Data Storage/save_directory", root + "/external-saves");
            ini.setValue("UI/Paths/gamedirs/size", 1);
            ini.setValue("UI/Paths/gamedirs/1/path", data + "/games");
            ini.setValue("Services/switchnet_username", "test-user");
            ini.setValue("Services/private_server_ca_bundle", root + "/custom-ca.pem");
            ini.setValue("Renderer/fx_preset_path", data + "/post_shaders/custom.fx");
            ini.sync();
        }
        std::atomic_bool cancelled{true};
        Check(!EdenTransfer::Copy({data, config}, root + "/cancel", root + "/cancel/config", true, &cancelled).ok, "cancelled transfer rejected");
        Check(!QFile::exists(root + "/cancel/config/qt-config.ini"), "cancelled transfer writes nothing");
#ifndef Q_OS_WIN
        const auto cancel_source = root + "/cancel-source", cancel_target = root + "/cancel-target";
        Write(cancel_source + "/config/qt-config.ini", "[Services]\nswitchnet_username=test\n");
        Write(cancel_source + "/nand/large", QByteArray(32 * 1024 * 1024, 'x'));
        Write(cancel_target + "/config/qt-config.ini", "old-config");
        cancelled.store(false);
        std::atomic_bool done{false};
        std::thread cancel_worker([&] {
            while (!done.load()) {
                QFile file(cancel_target + "/config/qt-config.ini");
                if (file.open(QIODevice::ReadOnly) && file.readAll() != "old-config") { cancelled.store(true); return; }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
        auto cancelled_result = EdenTransfer::Copy({cancel_source, cancel_source + "/config"}, cancel_target, cancel_target + "/config", true, &cancelled);
        done.store(true);
        cancel_worker.join();
        Check(cancelled.load() && !cancelled_result.ok, "cancel during a transfer");
        Check(Read(cancel_target + "/config/qt-config.ini") == "old-config", "cancellation restores overwritten config");
        Check(!QFile::exists(cancel_target + "/nand/large"), "cancellation removes incomplete data files");
#endif
        const auto original = Read(config + "/qt-config.ini");
        const auto minimal = root + "/minimal", full = root + "/full";
        Write(minimal + "/config/qt-config.ini", "original-config");
        auto result = EdenTransfer::Copy({data, config}, minimal, minimal + "/config", false);
        Check(result.ok, qPrintable(result.error));
        Check(Read(result.backup + "/config/qt-config.ini") == "original-config", "existing config backup");
        Check(!QFile::exists(minimal + "/nand/save/test"), "configs-only must not copy saves");
        QSettings minimal_ini(minimal + "/config/qt-config.ini", QSettings::IniFormat);
        Check(minimal_ini.value("Data Storage/nand_directory").toString() == data + "/nand", "keep original NAND path");
        Check(!minimal_ini.value("Data Storage/nand_directory/default").toBool(), "explicit storage path override");
        Check(QFile::exists(minimal + "/config/custom/game.ini"), "per-game configs copied");
        result = EdenTransfer::Copy({data, config}, full, full + "/config", true);
        Check(result.ok, qPrintable(result.error));
        Check(Read(full + "/nand/save/test") == "save-data", "full NAND copy");
        Check(Read(full + "/nand/save/game.ini") == Read(data + "/nand/save/game.ini"), "guest INI files must not be rewritten");
        Check(Read(full + "/sdmc/content") == "sd-data", "custom internal storage copy");
        Check(Read(full + "/saves/test") == "external-save", "external save copy");
        Check(Read(full + "/keys/prod.keys") == "test-only-keys", "full key copy");
        Check(Read(full + "/keys/grid0-ca.pem") == "test-only-public-ca", "custom CA bundle copy");
        QSettings full_ini(full + "/config/qt-config.ini", QSettings::IniFormat);
        Check(full_ini.value("Services/private_server_ca_bundle").toString() == full + "/keys/grid0-ca.pem", "custom CA path rewrite");
        Check(full_ini.value("Renderer/fx_preset_path").toString() == full + "/post_shaders/custom.fx", "copied shader path rewrite");
        Check(full_ini.value("Data Storage/sdmc_directory").toString() == full + "/sdmc", "rewritten storage path");
        Check(full_ini.value("Data Storage/save_directory").toString() == full + "/saves", "rewritten external save path");
        Check(full_ini.value("UI/Paths/gamedirs/1/path").toString() == data + "/games", "keep game library path");
        Check(Read(config + "/qt-config.ini") == original, "source config unchanged");
        Write(root + "/external-nand/save/external", "external-nand");
        {
            QSettings ini(config + "/qt-config.ini", QSettings::IniFormat);
            ini.setValue("Data Storage/nand_directory", root + "/external-nand");
            ini.setValue("Data Storage/save_directory", "");
            ini.sync();
        }
        const auto external = root + "/full-external";
        result = EdenTransfer::Copy({data, config}, external, external + "/config", true);
        Check(result.ok, qPrintable(result.error));
        Check(Read(external + "/nand/save/external") == "external-nand", "external NAND copy");
        Check(!QFile::exists(external + "/nand/save/test"), "unused NAND must not be merged");
        QSettings external_ini(external + "/config/qt-config.ini", QSettings::IniFormat);
        Check(external_ini.value("Data Storage/save_directory").toString() == external + "/nand", "empty save path follows configured NAND");
        {
            QSettings ini(config + "/qt-config.ini", QSettings::IniFormat);
            ini.setValue("Data Storage/nand_directory", QDir::rootPath());
            ini.sync();
        }
        Check(!EdenTransfer::Copy({data, config}, root + "/root-overlap", root + "/root-overlap/config", true).ok, "reject drive-root NAND containing destination");
        {
            QSettings ini(config + "/qt-config.ini", QSettings::IniFormat);
            ini.setValue("Data Storage/nand_directory", root + "/external-nand");
            ini.sync();
        }
        Check(!EdenTransfer::Copy({data, config}, data, data + "/config", true).ok, "reject overlapping roots");
        Write(root + "/collision/config/custom/game.ini/file", "directory collision");
        Check(!EdenTransfer::Copy({data, config}, root + "/collision", root + "/collision/config", false).ok, "reject target directory collision");
#ifndef Q_OS_WIN
        Check(QFile::link(root + "/external-saves", root + "/external-nand/link"), "create symlink fixture");
        Check(!EdenTransfer::Copy({data, config}, root + "/linked", root + "/linked/config", true).ok, "reject linked data trees");
#endif
        std::puts("Eden transfer: configs-only, full copy, external paths, backup, source preservation, overlap, collision, cancellation/rollback and symlink checks passed.");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
