// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <sys/wait.h>
#include "yuzu/startup_checks.h"

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Script(const QString& name, const QByteArray& contents) {
    QFile file(name);
    Check(file.open(QIODevice::WriteOnly), "open script");
    Check(file.write(contents) == contents.size(), "write script");
    Check(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner), "chmod");
}
int Run(const QByteArray& path) {
    const auto pid = SpawnChild(path.constData());
    Check(pid > 0, "fork child");
    int status{};
    Check(waitpid(pid, &status, 0) == pid && WIFEXITED(status), "wait child");
    return WEXITSTATUS(status);
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        Check(temp.isValid(), "temporary directory");
        const auto root = temp.path() + "/AppDir with spaces";
        Check(QDir().mkpath(root), "create AppDir");
        const auto child = QFile::encodeName(root + "/citrosis");
        const auto launcher = QFile::encodeName(root + "/AppRun");
        Script(QString::fromLocal8Bit(child), "#!/bin/sh\n[ \"$YUZU_DO_STARTUP_CHECKS\" = ON ] || exit 41\n[ \"${CITROSIS_TEST_LOADER_CONTEXT:-}\" = present ] || exit 42\nexit 0\n");
        Script(QString::fromLocal8Bit(launcher), "#!/bin/sh\nexport CITROSIS_TEST_LOADER_CONTEXT=present\nexec \"$(dirname -- \"$0\")/citrosis\"\n");
        qunsetenv("CITROSIS_APPIMAGE_LAUNCHER");
        qunsetenv("CITROSIS_TEST_LOADER_CONTEXT");
        qputenv(STARTUP_CHECK_ENV_VAR, ENV_VAR_ENABLED_TEXT);
        Check(Run(child) == 42, "direct exec lacks launcher setup");
        qputenv("CITROSIS_APPIMAGE_LAUNCHER", launcher);
        Check(Run(child) == 0, "AppImage probe preserves launcher setup and child marker");
        qputenv("CITROSIS_APPIMAGE_LAUNCHER", launcher + ".missing");
        Check(Run(child) == 127, "failed launcher must not report success");
        qunsetenv("CITROSIS_APPIMAGE_LAUNCHER");
        qputenv("CITROSIS_TEST_LOADER_CONTEXT", "present");
        Check(Run(child) == 0, "normal desktop execution still works");
        std::puts("Startup process regression passed.");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
