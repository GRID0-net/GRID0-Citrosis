#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The SDK is extracted locally; no root installation or source publication.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
if [[ ${1:-} == --setup ]]; then
    mkdir -p .cache/windows-rpms .cache/windows-sdk
    dnf --setopt=logdir=/tmp download --resolve --alldeps --destdir=.cache/windows-rpms \
        mingw64-gcc-c++ mingw64-qt6-qtbase mingw64-qt6-qtcharts mingw64-qt6-qtsvg \
        mingw64-SDL3 mingw64-openssl mingw64-zstd mingw64-libusb1 mingw64-zlib \
        mingw64-vulkan-headers mingw64-vulkan-utility-libraries
    python3 - <<'PY'
import pathlib, subprocess
sdk = pathlib.Path('.cache/windows-sdk').resolve()
for rpm in sorted(pathlib.Path('.cache/windows-rpms').glob('mingw*.rpm')):
    process = subprocess.Popen(['rpm2cpio', str(rpm.resolve())], stdout=subprocess.PIPE)
    subprocess.run(['cpio', '-idm', '--quiet'], stdin=process.stdout, cwd=sdk, check=True)
    process.stdout.close()
    if process.wait():
        raise SystemExit(f'Failed to extract {rpm}')
# A few Fedora CMake exports contain absolute SDK paths.
old = '/usr/x86_64-w64-mingw32/sys-root/mingw'
for path in (sdk / old.lstrip('/')).rglob('*.cmake'):
    original = path.read_text()
    path.write_text(original.replace(old, str(sdk) + old))
PY
    shift
fi
jobs=${1:-2}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'Usage: build-cross.sh [--setup] [jobs]' >&2; exit 2; }
cmake -S . -B build-windows-x64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/CMakeModules/toolchains/FedoraWindowsX64.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' -DENABLE_LTO=OFF \
    -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_UPDATE_CHECKER=OFF \
    -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_STATIC_BUILD=OFF -DYUZU_USE_BUNDLED_FFMPEG=ON \
    -DYUZU_USE_BUNDLED_SDL3=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF \
    -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
    -DENABLE_WERROR=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_CMD=ON -DYUZU_ROOM=ON \
    -DYUZU_ROOM_STANDALONE=ON -DYUZU_TESTS=OFF -DYUZU_BUILD_PRESET=generic \
    -DENABLE_LSFG=ON -DSDL3_FORCE_SYSTEM=ON
cmake --build build-windows-x64 --target yuzu yuzu-cmd yuzu_room_standalone --parallel "$jobs"
echo 'Package with: python3 tools/windows/package-cross.py --build build-windows-x64 --sdk .cache/windows-sdk --output build-windows-x64/package/citrosis-windows-x64-private'
