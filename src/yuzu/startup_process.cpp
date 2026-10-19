// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _WIN32
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "yuzu/startup_checks.h"

pid_t SpawnChild(const char* arg0) {
    // AppRun invokes a bundled ELF loader with --library-path. Those options
    // are not inherited by exec, so executing the binary directly would use
    // the host loader/libraries and could fail before Vulkan is even reached.
    const char* launcher = std::getenv("CITROSIS_APPIMAGE_LAUNCHER");
    const bool use_launcher = launcher && launcher[0] != '\0';
    const pid_t pid = fork();
    if (pid == -1) {
        std::fprintf(stderr, "Vulkan startup check: fork failed with error %d\n", errno);
    } else if (pid == 0) {
        if (use_launcher) {
            execl(launcher, launcher, nullptr);
        } else {
            execlp(arg0, arg0, nullptr);
        }
        std::fprintf(stderr, "Vulkan startup check: could not execute %s (error %d)\n",
                     use_launcher ? launcher : arg0, errno);
        _exit(127);
    }
    return pid;
}
#endif
