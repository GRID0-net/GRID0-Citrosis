#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build Citrosis desktop release packages in one step.
#
#   ./build-release.sh                    # Linux x86_64 AppImage (the default target)
#   ./build-release.sh linux-x86_64 linux-aarch64 windows-x86_64
#   ./build-release.sh all --jobs 4 --source
#
# Targets and where they can be built:
#   linux-x86_64    native on x86_64 Linux, or cross-built on Fedora ARM64 (extracted x86_64 SDK)
#   linux-aarch64   native on ARM64 Linux
#   windows-x86_64  cross-built on Fedora (extracted MinGW SDK); zip with DLLs and Qt plugins
#
# Everything lands in artifacts/Citrosis-<date>-<commit>[-dirty]/ with SHA256SUMS, BUILD-INFO.json
# and one log per step under logs/. Build directories are reused, so later runs are incremental.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$root"

usage() {
    sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'
Options:
  --jobs N        parallel compile jobs (default: from CPU count and RAM, about 3 GiB per job)
  --setup         (re)download the cross SDKs even if they already exist
  --source        also write Citrosis-source.tar.gz (tracked files + pinned dependencies)
  --name NAME     output folder name under artifacts/ (default Citrosis-<date>-<commit>)
  --clean         delete the build directories of the selected targets first
  --keep-appdir   keep the unpacked .AppDir next to each AppImage
  -h, --help      show this help
EOF
}

targets=()
jobs=""
force_setup=0
make_source=0
name=""
clean=0
keep_appdir=0
while (($#)); do
    case "$1" in
        linux-x86_64|linux-aarch64|windows-x86_64) targets+=("$1") ;;
        all) targets+=(linux-x86_64 linux-aarch64 windows-x86_64) ;;
        --jobs) shift; jobs=${1:?--jobs needs a number} ;;
        --setup) force_setup=1 ;;
        --source) make_source=1 ;;
        --name) shift; name=${1:?--name needs a value} ;;
        --clean) clean=1 ;;
        --keep-appdir) keep_appdir=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
((${#targets[@]})) || targets=(linux-x86_64)
# Drop duplicates, keep order.
declare -A seen=()
unique=()
for t in "${targets[@]}"; do [[ -n ${seen[$t]:-} ]] || { seen[$t]=1; unique+=("$t"); }; done
targets=("${unique[@]}")

host_arch=$(uname -m)
[[ $(uname -s) == Linux ]] || { echo 'Run this on Linux (Windows users: see build.md for MSYS2).' >&2; exit 1; }

if [[ -z $jobs ]]; then
    cpus=$(nproc)
    mem_gib=$(awk '/MemTotal/ { printf "%d", $2 / 1048576 }' /proc/meminfo)
    jobs=$(( mem_gib / 3 ))
    (( jobs < 1 )) && jobs=1
    (( jobs > cpus )) && jobs=$cpus
fi
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be a positive number' >&2; exit 2; }

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
need() { for tool in "$@"; do command -v "$tool" >/dev/null || die "missing tool: $tool (see build.md)"; done; }
is_fedora() { [[ -r /etc/os-release ]] && grep -qi '^ID=fedora' /etc/os-release; }

# Check every selected target before spending an hour compiling the first one.
need git cmake ninja python3 clang clang++
for t in "${targets[@]}"; do
    case "$t" in
        linux-x86_64)
            need mksquashfs
            command -v llvm-readelf >/dev/null || command -v readelf >/dev/null || die 'missing readelf'
            if [[ $host_arch != x86_64 ]]; then
                [[ $host_arch == aarch64 ]] || die "linux-x86_64 cannot be built on $host_arch"
                is_fedora || [[ -d .cache/linux-x86_64-sdk/usr ]] ||
                    die 'cross-building linux-x86_64 needs Fedora (dnf) or an existing .cache/linux-x86_64-sdk'
                need rpm2cpio cpio
            fi ;;
        linux-aarch64)
            [[ $host_arch == aarch64 ]] || die 'linux-aarch64 must be built on an ARM64 Linux host'
            need mksquashfs ;;
        windows-x86_64)
            is_fedora || [[ -d .cache/windows-sdk/usr ]] ||
                die 'windows-x86_64 is cross-built on Fedora (or with an existing .cache/windows-sdk)'
            need rpm2cpio cpio zip
            command -v x86_64-w64-mingw32-objdump >/dev/null || command -v llvm-objdump >/dev/null ||
                command -v objdump >/dev/null || die 'missing objdump' ;;
    esac
done

commit=$(git rev-parse --short=10 HEAD)
dirty=""
[[ -z $(git status --porcelain --untracked-files=no) ]] || dirty="-dirty"
[[ -n $name ]] || name="Citrosis-$(date +%Y-%m-%d)-${commit}${dirty}"
out="$root/artifacts/$name"
if [[ -e $out ]]; then
    n=2
    while [[ -e "$out-$n" ]]; do n=$((n + 1)); done
    out="$out-$n"
fi
logs="$out/logs"
work="$out/.work"
mkdir -p "$logs" "$work"

say "Citrosis release build"
echo "commit:  $(git rev-parse HEAD)${dirty:+ (with uncommitted changes)}"
echo "targets: ${targets[*]}"
echo "host:    $host_arch, $jobs job(s)"
echo "output:  $out"
[[ -z $dirty ]] || echo 'note:    the build includes uncommitted changes (marked -dirty).'

# Runs a step, keeping its full output in logs/ and showing the tail if it fails.
run() {
    local log="$logs/$1.log"; shift
    echo "  \$ $*  (log: logs/$(basename "$log"))"
    if ! "$@" >"$log" 2>&1; then
        echo '----- last 40 lines -----' >&2
        tail -n 40 "$log" >&2
        die "step failed, full log: $log"
    fi
}

# The checkout may have moved since the last build (for example ~/citrosis-android ->
# ~/projects/citrosis-android). CMake caches and the extracted SDKs' CMake exports hold absolute
# paths, so re-point the SDKs and start over in build folders created somewhere else.
relocate_sdk() {
    local sdk=$1
    [[ -d $sdk ]] || return 0
    python3 - "$root" "$sdk" <<'PY'
import pathlib, re, sys
root, sdk = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
name = sdk.name
pattern = re.compile(r'(/[^"\s;:]*?)/\.cache/' + re.escape(name) + r'(?=/|")')
changed = 0
for path in sdk.rglob('*.cmake'):
    if path.is_symlink() or not path.is_file():
        continue
    text = path.read_text(errors='surrogateescape')
    new = pattern.sub(lambda m: m.group(0) if m.group(1) == str(root) else f'{root}/.cache/{name}', text)
    if new != text:
        path.write_text(new, errors='surrogateescape')
        changed += 1
if changed:
    print(f'  re-pointed {changed} CMake files in .cache/{name} to {root}')
PY
}
check_build_dir() {
    local dir=$1 cache="$1/CMakeCache.txt" home
    [[ -f $cache ]] || return 0
    home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")
    if [[ -n $home && $home != "$root" ]]; then
        echo "  $(basename "$dir") was configured for $home; removing it for a fresh build here"
        rm -rf "$dir"
    fi
}
for t in "${targets[@]}"; do
    case "$t" in
        linux-x86_64)
            relocate_sdk "$root/.cache/linux-x86_64-sdk"
            check_build_dir "$root/build-appimage-x86_64"
            check_build_dir "$root/build-appimage-x86_64-native" ;;
        linux-aarch64) check_build_dir "$root/build-appimage-aarch64" ;;
        windows-x86_64)
            relocate_sdk "$root/.cache/windows-sdk"
            check_build_dir "$root/build-windows-x64" ;;
    esac
done

source_archive=""
if ((make_source)); then
    say "Source archive"
    source_archive="$out/Citrosis-source.tar.gz"
    run source python3 tools/release/package-source.py --output "$source_archive"
fi
source_args=()
[[ -z $source_archive ]] || source_args=(--source-archive "$source_archive")

# Same options as build-citrosis-desktop.sh, Release, no tests, for packaging.
configure_native() {
    local dir=$1
    cmake -S "$root" -B "$dir" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=OFF \
        -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
        -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_UPDATE_CHECKER=OFF \
        -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_STATIC_BUILD=OFF \
        -DYUZU_USE_EXTERNAL_FFMPEG=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
        -DYUZU_USE_BUNDLED_SDL3=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF \
        -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
        -DENABLE_WERROR=OFF -DUSE_DISCORD_PRESENCE=OFF \
        -DYUZU_CMD=ON -DYUZU_ROOM=ON -DYUZU_ROOM_STANDALONE=ON \
        -DYUZU_TESTS=OFF -DYUZU_BUILD_PRESET=generic -DENABLE_LSFG=ON
}
build_native() {
    cmake --build "$1" --target yuzu yuzu-cmd yuzu_room_standalone --parallel "$jobs"
}

package_appimage() {
    local arch=$1 build=$2; shift 2
    local image="$work/Citrosis-Linux-$arch.AppImage"
    rm -rf "$image" "$work/Citrosis-Linux-$arch.AppDir" "$image.sha256"
    run "package-linux-$arch" python3 tools/linux/package-appimage.py \
        --build "$build" --arch "$arch" --output "$image" "${source_args[@]}" "$@"
    mv "$image" "$image.sha256" "$out/"
    if ((keep_appdir)); then
        mv "$work/Citrosis-Linux-$arch.AppDir" "$out/"
    else
        rm -rf "$work/Citrosis-Linux-$arch.AppDir"
    fi
    echo "  -> $out/Citrosis-Linux-$arch.AppImage"
}

build_linux_x86_64() {
    say "Linux x86_64 AppImage"
    if [[ $host_arch == x86_64 ]]; then
        local dir="$root/build-appimage-x86_64-native"
        ((clean)) && rm -rf "$dir"
        run configure-linux-x86_64 configure_native "$dir"
        run build-linux-x86_64 build_native "$dir"
        package_appimage x86_64 "$dir"
    else
        # Fedora ARM64 host: cross-compile against an extracted Fedora x86_64 SDK.
        ((clean)) && rm -rf "$root/build-appimage-x86_64"
        if ((force_setup)) || [[ ! -d .cache/linux-x86_64-sdk/usr ]]; then
            run setup-linux-x86_64-sdk bash tools/linux/setup-fedora-x64-sdk.sh
        fi
        run build-linux-x86_64 bash tools/linux/build-linux-x64.sh "$jobs"
        package_appimage x86_64 "$root/build-appimage-x86_64" --sysroot "$root/.cache/linux-x86_64-sdk"
    fi
}

build_linux_aarch64() {
    say "Linux aarch64 AppImage"
    local dir="$root/build-appimage-aarch64"
    ((clean)) && rm -rf "$dir"
    run configure-linux-aarch64 configure_native "$dir"
    run build-linux-aarch64 build_native "$dir"
    package_appimage aarch64 "$dir"
}

build_windows_x86_64() {
    say "Windows x86_64 zip"
    ((clean)) && rm -rf "$root/build-windows-x64"
    local setup=()
    if ((force_setup)) || [[ ! -d .cache/windows-sdk/usr ]]; then setup=(--setup); fi
    run build-windows-x86_64 bash tools/windows/build-cross.sh "${setup[@]}" "$jobs"
    local folder="$work/Citrosis-Windows-x86_64"
    rm -rf "$folder"
    run package-windows-x86_64 python3 tools/windows/package-cross.py --release \
        --build build-windows-x64 --sdk .cache/windows-sdk --output "$folder" "${source_args[@]}"
    (cd "$work" && run zip-windows-x86_64 zip -qr9 "$out/Citrosis-Windows-x86_64.zip" Citrosis-Windows-x86_64)
    rm -rf "$folder"
    echo "  -> $out/Citrosis-Windows-x86_64.zip"
}

start=$(date +%s)
for t in "${targets[@]}"; do
    case "$t" in
        linux-x86_64) build_linux_x86_64 ;;
        linux-aarch64) build_linux_aarch64 ;;
        windows-x86_64) build_windows_x86_64 ;;
    esac
done
rmdir "$work" 2>/dev/null || rm -rf "$work"

say "Checksums"
(
    cd "$out"
    find . -maxdepth 1 -type f \( -name '*.AppImage' -o -name '*.zip' -o -name '*.tar.gz' \) \
        -printf '%f\n' | sort | xargs -r sha256sum > SHA256SUMS
    cat SHA256SUMS
)
python3 - "$out" "${targets[@]}" <<PY
import json, sys, platform, subprocess, datetime, pathlib
out = pathlib.Path(sys.argv[1])
info = {
    "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "source_tree_clean": "$dirty" == "",
    "targets": sys.argv[2:],
    "built_on": f"{platform.system()} {platform.machine()}",
    "built_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    "jobs": $jobs,
    "files": sorted(p.name for p in out.iterdir() if p.is_file() and p.suffix in (".AppImage", ".zip", ".gz")),
}
(out / "BUILD-INFO.json").write_text(json.dumps(info, indent=2) + "\n")
PY
git rev-parse HEAD > "$out/SOURCE_COMMIT.txt"

elapsed=$(( $(date +%s) - start ))
say "Done in $((elapsed / 60))m $((elapsed % 60))s"
echo "$out"
