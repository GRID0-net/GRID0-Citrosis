#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Requires an extracted Fedora x86_64 SDK under .cache/linux-x86_64-sdk.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
cmake -S . -B build-appimage-x86_64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/CMakeModules/toolchains/FedoraLinuxX64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
  -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' -DENABLE_LTO=OFF \
  -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_UPDATE_CHECKER=OFF \
  -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_STATIC_BUILD=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
  -DYUZU_USE_EXTERNAL_FFMPEG=OFF -DYUZU_USE_BUNDLED_SDL3=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF \
  -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
  -DENABLE_WERROR=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_CMD=ON -DYUZU_ROOM=ON \
  -DYUZU_ROOM_STANDALONE=ON -DYUZU_TESTS=OFF -DYUZU_BUILD_PRESET=generic -DENABLE_LSFG=ON
cmake --build build-appimage-x86_64 --target yuzu yuzu-cmd yuzu_room_standalone --parallel "${1:-2}"
