#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the Citrosis Linux and Windows release packages from macOS.
#
#   ./build-release-mac.sh                          # Linux x86_64 AppImage
#   ./build-release-mac.sh all --source             # both AppImages, Windows zip, source archive
#   ./build-release-mac.sh linux-x86_64 windows-x86_64 --jobs 4
#
# build-release.sh is made for Fedora ARM64, which cross-builds linux-x86_64 and windows-x86_64
# and builds linux-aarch64 natively. This wrapper runs it unchanged inside a Fedora 44 container
# (Docker Desktop, OrbStack, Colima or Podman). On Apple Silicon the container is native ARM64,
# so no emulation is involved. On an Intel Mac it is x86_64: linux-x86_64 and windows-x86_64 work,
# linux-aarch64 does not.
#
# The checkout is copied into a container volume and built there, because macOS folders are
# case-insensitive by default and the extracted Fedora/MinGW SDKs contain files whose names differ
# only by case. The volume also keeps the SDKs, CPM downloads and build folders, so later runs are
# incremental. Finished packages are copied back to ./artifacts/. Uncommitted changes are included
# (the output folder is then marked -dirty, as with build-release.sh).
#
# Wrapper options (everything else goes to build-release.sh, see ./build-release.sh --help):
#   --shell           open a shell in the build container instead of building
#   --rebuild-image   rebuild the Fedora builder image (picks up package updates)
#   --reset           delete the build volume (SDKs, caches, build folders) and exit
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
image=citrosis-fedora-builder:44
volume=citrosis-release-build
repo_name=$(basename "$root")

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || die 'This wrapper is for macOS. On Linux run ./build-release.sh directly.'

shell=0
rebuild_image=0
reset=0
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        --shell) shell=1 ;;
        --rebuild-image) rebuild_image=1 ;;
        --reset) reset=1 ;;
        -h|--help)
            sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) args+=("$1") ;;
    esac
    shift
done

# Container engine: Docker CLI (Docker Desktop, OrbStack, Colima) or Podman.
engine=""
for candidate in docker podman; do
    if command -v "$candidate" >/dev/null 2>&1; then engine=$candidate; break; fi
done
[ -n "$engine" ] || die 'Install a container runtime first: OrbStack, Docker Desktop, Colima or Podman.'
"$engine" info >/dev/null 2>&1 ||
    die "$engine is installed but its engine is not running (start OrbStack/Docker Desktop, 'colima start' or 'podman machine start')."

case "$(uname -m)" in
    arm64|aarch64) platform=linux/arm64 ;;
    x86_64) platform=linux/amd64 ;;
    *) die "Unsupported Mac architecture: $(uname -m)" ;;
esac

if [ $reset -eq 1 ]; then
    say "Removing volume $volume"
    "$engine" volume rm "$volume" >/dev/null 2>&1 || true
    exit 0
fi

if [ $rebuild_image -eq 1 ] || ! "$engine" image inspect "$image" >/dev/null 2>&1; then
    say "Building $image ($platform)"
    # Packages: build.md's Fedora list, plus what the cross SDK setup and packaging steps call
    # (dnf download, rpm2cpio, cpio, zip, llvm-readelf/objdump, mksquashfs) and rsync for the copy.
    "$engine" build --platform "$platform" -t "$image" - <<'DOCKERFILE'
FROM registry.fedoraproject.org/fedora:44
RUN dnf -y install --setopt=install_weak_deps=False \
        git clang lld llvm cmake ninja-build gcc-c++ python3 make patch \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtcharts-devel qt6-qtsvg-devel \
        SDL3-devel ffmpeg-free-devel openssl-devel zlib-devel libzstd-devel lz4-devel \
        libusb1-devel libdrm-devel glslang nasm yasm \
        alsa-lib-devel pulseaudio-libs-devel libX11-devel libXext-devel libXrandr-devel \
        libXi-devel libXcursor-devel libXinerama-devel wayland-devel libxkbcommon-devel \
        mesa-vulkan-drivers vulkan-loader squashfs-tools binutils qt6-qtwayland \
        dnf5-plugins rpm cpio zip unzip xz rsync findutils which file procps-ng \
    && dnf clean all \
    && git config --system --add safe.directory '*'
WORKDIR /work
DOCKERFILE
fi

"$engine" volume inspect "$volume" >/dev/null 2>&1 || "$engine" volume create "$volume" >/dev/null
mkdir -p "$root/artifacts"

tty_flag=()
[ -t 0 ] && [ -t 1 ] && tty_flag=(-t)

# Inside the container: mirror the checkout (tracked and untracked source, .git included) into the
# volume, keeping the volume's own caches, build folders and artifacts, then run build-release.sh
# and copy the new output folder back to the Mac.
inner=$(cat <<'INNER'
set -euo pipefail
src=/src
dst="/work/$REPO_NAME"
mkdir -p "$dst"
echo "Syncing checkout into the build volume..."
rsync -a --delete \
    --exclude='/.cache/' --exclude='/artifacts/' --exclude='/build-*/' \
    --exclude='/build-desktop*/' --exclude='.DS_Store' \
    "$src/" "$dst/"
cd "$dst"
git update-index -q --refresh >/dev/null 2>&1 || true
if [ "$WRAPPER_SHELL" = 1 ]; then
    echo "Build tree: $dst   (run ./build-release.sh ... ; copy results to /out)"
    exec bash -l
fi
before=$(ls -1 artifacts 2>/dev/null | sort || true)
./build-release.sh "$@"
after=$(ls -1 artifacts 2>/dev/null | sort || true)
new=$(comm -13 <(printf '%s\n' "$before") <(printf '%s\n' "$after") | grep -v '^$' || true)
[ -n "$new" ] || { echo 'build-release.sh produced no new output folder' >&2; exit 1; }
for folder in $new; do
    echo "Copying artifacts/$folder to the Mac..."
    rsync -a --exclude='.work/' "artifacts/$folder" /out/
done
INNER
)

say "Running build-release.sh ${args[*]+"${args[*]}"} in Fedora 44 ($platform, $engine)"
# ${array[@]+...} keeps empty arrays from tripping set -u in macOS's bash 3.2.
"$engine" run --rm -i ${tty_flag[@]+"${tty_flag[@]}"} --platform "$platform" \
    -e REPO_NAME="$repo_name" -e WRAPPER_SHELL="$shell" \
    -v "$root:/src:ro" \
    -v "$volume:/work" \
    -v "$root/artifacts:/out" \
    "$image" bash -c "$inner" bash ${args[@]+"${args[@]}"}

[ $shell -eq 1 ] || say "Done. Packages are in $root/artifacts/"
