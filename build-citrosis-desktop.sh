#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$root/build-desktop"
jobs=2
build_type=Release
run_tests=0
extra=()
while (($#)); do
    case "$1" in
        --jobs) shift; jobs=${1:?--jobs requires a number} ;;
        --build-dir) shift; build_dir=${1:?--build-dir requires a path} ;;
        --debug) build_type=Debug ;;
        --test) run_tests=1 ;;
        --) shift; extra+=("$@"); break ;;
        --help|-h)
            echo 'Usage: ./build-citrosis-desktop.sh [--jobs N] [--build-dir PATH] [--debug] [--test] [-- CMake options]'
            echo 'Linux or Windows MSYS2 CLANG64. Dependencies: docs/BUILDING-CITROSIS-DESKTOP.md'
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be positive.' >&2; exit 2; }
case "$(uname -s)" in
    Linux) ;;
    MINGW*|MSYS*)
        [[ ${MSYSTEM:-} == CLANG64 ]] || { echo 'Open the MSYS2 CLANG64 terminal first.' >&2; exit 1; } ;;
    *) echo 'This wrapper supports Linux and Windows MSYS2 CLANG64.' >&2; exit 1 ;;
esac
cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE="$build_type" -DENABLE_LTO=OFF \
    -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_C_FLAGS_DEBUG='-O0 -g0' -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' \
    -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_UPDATE_CHECKER=OFF \
    -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_STATIC_BUILD=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
    -DYUZU_USE_BUNDLED_SDL3=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF \
    -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
    -DENABLE_WERROR=OFF -DUSE_DISCORD_PRESENCE=OFF \
    -DYUZU_CMD=ON -DYUZU_ROOM=ON -DYUZU_ROOM_STANDALONE=ON \
    -DYUZU_TESTS=ON -DBUILD_TESTING=ON -DYUZU_BUILD_PRESET=generic \
    -DENABLE_LSFG=ON "${extra[@]}"
test_targets=(tests memory_regressions eden_transfer_tests)
if [[ $(uname -s) == Linux ]]; then test_targets+=(startup_process_tests); fi
cmake --build "$build_dir" --target yuzu yuzu-cmd yuzu_room_standalone "${test_targets[@]}" --parallel "$jobs"
if ((run_tests)); then
    ctest --test-dir "$build_dir" --output-on-failure
fi
echo "Executables are in $build_dir/bin"
