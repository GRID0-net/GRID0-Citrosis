# Citrosis on Windows and Linux

This desktop port uses the **Lemon-based Citrosis core from the Android branch**,
including its GRID0+ networking, shader-pool leak fix, Vulkan changes, ReShade FX,
and frame generation. It restores the matching Qt/SDL/room frontends from before
the Android-only trim. It does not switch back to the older Citron core on `main`.
See `citrosis-desktop-port.json` for the source commits.

## Desktop menus and Eden transfer

**GRID0+ → Settings** opens account and network controls directly. **GRID0+ →
Friends** opens friend codes and requests; **GRID0+ → Check patches** reports
“All patches are up to date” when the installed files match the latest manifest.
Startup checks stay quiet when current. A failed manual check reports its error
and keeps installed patches.

**Tools → Transfer from Eden** opens the desktop-only wizard. A fresh profile
also offers it when Eden is detected. Detection covers the standard Windows
`%APPDATA%/eden` and Linux XDG data/config locations; browse to an Eden install,
portable `user` folder, or config folder for other layouts. Close Eden and stop
emulation before copying.

* **Full transfer** copies global/per-game configs, keys, NAND/SD content, saves,
  save states, mods, play time, custom shaders/presets, optional frame-generation
  files, GRID0+ patches, amiibo, TAS files, dumps, and screenshots. Configured
  external NAND/save locations are copied into Citrosis too. Storage, shader,
  and custom-CA paths are adjusted to the copies. Save states are copied unchanged.
* **Configs and paths only** copies configuration files and keeps their data
  paths pointing to Eden's existing content; keys are included only in Full
  transfer. Games remain at their existing
  library paths in both modes; shader caches are not copied.

Replaced Citrosis files are backed up under `transfer-backups/<timestamp-id>` in
its data folder. Eden files are copied, never moved or deleted. Cancelling rolls
back completed writes. Linked entries inside data trees are rejected; resolve
those entries or use configs-only transfer. Citrosis restarts to load the imported
profile, and the closing process cannot overwrite the imported configuration.

## Linux: Fedora 44, x86_64 or aarch64

Install the build dependencies:

```sh
sudo dnf install git clang lld cmake ninja-build gcc-c++ python3 \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtcharts-devel qt6-qtsvg-devel \
  SDL3-devel ffmpeg-free-devel openssl-devel zlib-devel libzstd-devel lz4-devel \
  libusb1-devel libdrm-devel glslang nasm yasm \
  alsa-lib-devel pulseaudio-libs-devel libX11-devel libXext-devel libXrandr-devel \
  libXi-devel libXcursor-devel libXinerama-devel wayland-devel libxkbcommon-devel
```

Build from this branch:

```sh
bash ./build-citrosis-desktop.sh --jobs 2 --test
./build-desktop/bin/citrosis
```

Use `--jobs 1` on an 8 GB machine or when other applications are using memory.
The first build downloads pinned dependencies into `.cache/cpm`; it needs Internet
access. The wrapper uses Clang, system Qt/SDL/FFmpeg/OpenSSL, bundled Vulkan
headers, and disables LTO to reduce build memory. Release uses `-O2`; `--debug`
builds without debug symbols to keep memory requirements down. Use a separate
`--build-dir build-desktop-release` when switching between debug and release.

Install locally, if wanted:

```sh
cmake --install build-desktop --prefix "$HOME/.local"
```

The CI `.tar.gz` archives are **Fedora 44 binaries**. The separately packaged
AppImages below bundle their runtime libraries. Use the
matching architecture and install the runtime libraries:

```sh
sudo dnf install qt6-qtbase qt6-qtcharts qt6-qtsvg SDL3 ffmpeg-free \
  openssl-libs libusb1 vulkan-loader
```

A working Vulkan driver is also required for Vulkan rendering. Mesa drivers or
the appropriate proprietary GPU driver must be supplied by the host OS. Building
on other distributions is possible with equivalent development packages and
CMake 3.31 or later; their binary compatibility is not guaranteed by the Fedora
archive.

## Linux AppImages

Build and package a release on the corresponding Fedora architecture:

```sh
sudo dnf install squashfs-tools binutils qt6-qtwayland
bash ./build-citrosis-desktop.sh --build-dir build-appimage --jobs 2 --test
mkdir -p artifacts
python3 tools/linux/package-appimage.py --build build-appimage --arch "$(uname -m)" \
  --output "artifacts/Citrosis-Linux-$(uname -m).AppImage"
```

The packager pins and verifies the Type 2 runtime, checks ELF architectures,
collects the executable/Qt-plugin dependency closure, and bundles Qt, OpenSSL,
public CA certificates, libc and its loader, plus library licenses. GPU drivers
come from the host. File hashes accompany the image. The launcher keeps Qt's
library/plugin paths within the bundle and prefers the host's public CA bundle.

The launcher also exports `CITROSIS_APPIMAGE_LAUNCHER` for the isolated Vulkan
startup probe. The probe re-enters AppRun so it uses the same bundled ELF loader
and library search path. Executing the raw binary instead loses the loader's
`--library-path` option and can fail on a different distribution before reaching
Vulkan, producing a misleading broken-Vulkan warning.

Run the image directly, or use extraction mode when FUSE is unavailable:

```sh
chmod +x Citrosis-Linux-aarch64.AppImage
./Citrosis-Linux-aarch64.AppImage
./Citrosis-Linux-aarch64.AppImage --appimage-extract-and-run
./Citrosis-Linux-aarch64.AppImage --cli --help
./Citrosis-Linux-aarch64.AppImage --room --help
```

Choose `x86_64` for Intel/AMD PCs and `aarch64` for ARM64 Linux. Both use the same
Citrosis core. On Fedora ARM64, an x86_64 release can be cross-built with:

```sh
bash tools/linux/setup-fedora-x64-sdk.sh
bash tools/linux/build-linux-x64.sh 2
python3 tools/linux/package-appimage.py --build build-appimage-x86_64 --arch x86_64 \
  --sysroot .cache/linux-x86_64-sdk --output artifacts/Citrosis-Linux-x86_64.AppImage
```

The SDK and pinned source caches stay local under `.cache`; no repository push
is needed. Host Qt tools must match the SDK's Qt version.

## Windows x86_64: MSYS2 CLANG64

Install [MSYS2](https://www.msys2.org/), update it, and open its **CLANG64** terminal.
Use one toolchain consistently, including Qt, FFmpeg, and SDL:

```sh
pacman -Syu
pacman -S --needed git make patch tar autoconf automake libtool \
  mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-python \
  mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-qt6-charts \
  mingw-w64-clang-x86_64-qt6-svg mingw-w64-clang-x86_64-SDL3 \
  mingw-w64-clang-x86_64-ffmpeg mingw-w64-clang-x86_64-openssl \
  mingw-w64-clang-x86_64-zlib mingw-w64-clang-x86_64-lz4 \
  mingw-w64-clang-x86_64-zstd mingw-w64-clang-x86_64-libusb \
  mingw-w64-clang-x86_64-glslang
bash ./build-citrosis-desktop.sh --jobs 2 --test
```

The GUI is `build-desktop/bin/citrosis.exe`. Package it with the Qt plugins and
recursive DLL dependencies before running outside CLANG64:

```sh
cmake -DBIN_DIR="$PWD/build-desktop/bin" \
  -DOUTPUT_DIR="$PWD/artifacts/Citrosis-windows-x86_64" \
  -DMINGW_PREFIX="$(cygpath -m "$MINGW_PREFIX")" -DSOURCE_DIR="$PWD" \
  -P tools/windows/package-citrosis.cmake
```

The Windows workflow checks the packaged CLI with MSYS2 removed from `PATH`.
Windows ARM64 is not a target of this port. Linux ARM64 keeps NCE on 4 KiB page
kernels; x86_64 uses Dynarmic. Hosts with 16/64 KiB pages, including Fedora Asahi,
fall back to Dynarmic with software memory access. Expect lower performance there;
this does not add NCE support for those page sizes. Windows ARM NCE would require
a separate signal/exception implementation.

## Private Windows cross-build on Fedora ARM64

The Windows x64 build can also be made locally without a Windows runner or
publishing a repository. With the Linux build dependencies above and Fedora's
`dnf download`, `rpm2cpio`, and `cpio` available:

```sh
bash tools/windows/build-cross.sh --setup 2
python3 tools/windows/package-cross.py \
  --build build-windows-x64 --sdk .cache/windows-sdk \
  --output build-windows-x64/package/citrosis-windows-x64-private
```

This extracts Fedora MinGW packages into `.cache/windows-sdk`; it does not
install them system-wide. Native Qt tools generate sources for the matching
Windows Qt version. FFmpeg uses the pinned MinGW dependency from `cpmfile.json`.
The build uses GCC, Release `-O2`, and no LTO. Rerun the build script without
`--setup` to reuse the SDK. The setup downloads transitive RPM dependencies,
but extracts only the MinGW packages.

The packaging script copies the three executables, required Qt plugins, and
the recursive DLL import closure. It rejects missing non-system imports and
non-x64 PE images, includes licenses and file hashes, and creates a ZIP. These
checks do not replace a native Windows launch and gameplay test.

## GitHub Actions

Push `codex/citrosis-desktop` to run `.github/workflows/desktop-build.yml`, or
choose **Citrosis desktop → Run workflow** on a branch containing that workflow.
The jobs build and test Linux x86_64, Linux ARM64, and Windows x86_64. Download
their artifacts from the completed workflow. This workflow does not publish a
release or upload account settings, keys, saves, or game files.

The existing Android workflow and `build-citrosis-android.sh` remain available.
The `citrosis-upstream.json` Android marker describes the core's origin and is
retained for that workflow's source check.

## Configuration and GRID0+

* Windows data: `%APPDATA%/citrosis`.
* Linux data: `$XDG_DATA_HOME/citrosis`, normally `~/.local/share/citrosis`.
* Linux configuration: `$XDG_CONFIG_HOME/citrosis`, normally `~/.config/citrosis`.
* A `user` directory beside the executable on Windows, or in the working
  directory on Linux, enables portable mode.
* Android keeps its existing data directory. Existing Eden/Citron data is not
  migrated automatically. Use **File → Open Citrosis Folder** to inspect the
  active data directory (some inherited menu text may still say Eden).

Set the GRID0+ username, password, login server, game server address, optional
secondary NAT address, and optional custom CA PEM path under **GRID0+ → Settings**
(or **Configure → System → Network**). Use **Apply and test GRID0+ login** to verify the account. This button
and **Apply and open GRID0+ Friends** apply the network settings immediately.
Credentials are saved
in the local configuration; the password field is masked. Change account settings
while emulation is stopped.

**GRID0+ → Friends** provides the friend list, friend-code requests,
incoming/outgoing requests, avatars, and presence through the same backend as
Android. **GRID0+ → Check patches** checks the shared GRID0 patch release,
asks before installing changes, verifies hashes, and restarts Citrosis after an
update. Configured GRID0 accounts also check at startup. This updates GRID0 game
patches, not the emulator binary. The upstream Lemon emulator updater is disabled.

## Performance settings and validation

The existing Qt settings and per-game overrides are restored. Citrosis memory
reclamation, bounded shader translation pools, frame generation, and the FX
settings are exposed alongside them. Desktop shader worker counts are capped by
host RAM. Android's thermal and memory-pressure monitoring remains Android-only.
Frame generation requires Vulkan; engine `0` is Citrosis FG and engine `1` uses
the user's own `lossless/Lossless.dll` inside the active data directory.
Optional extra FX files are in the archive's
`extras` folder; copy effects into the active data directory's `post_shaders`
folder. Built-in FX are seeded by the shared renderer.

The generic tests and `memory_regressions` run through CTest. A successful compile
does not establish Splatoon 3 performance or GRID0 compatibility. Those require
testing a user-provided game/firmware and account on each GPU/OS. Measure actual
emulated FPS separately from generated display frames, and compare plaza and
match scenes at the same resolution, settings, game version, and shader-cache
state. Keep the game's normal 30/60 FPS timing limits when comparing.

### Local port validation

On Fedora Asahi 44 ARM64 with Clang 22, Qt 6.11, FFmpeg 8, and a 16 KiB page kernel:

* Debug GUI, CLI, room server, and tests compile and link; Linux installation succeeds.
* All six CTest entries pass (including the Eden-transfer regression test). The main test executable passes 55 cases and skips
  16 fastmem mapping cases because this host cannot provide 4 KiB mappings.
* A headless GUI check opens and closes settings and GRID0+ Friends, and verifies
  frame generation, memory options, the account group, masked password, and friend-code entry.
* The local build disables optional libusb GameCube-adapter support. CI enables it.

Windows x86_64 GUI, CLI, and room-server Release executables have also been
cross-compiled locally using Fedora MinGW GCC 16.2, Qt 6.11.2, SDL 3.4, and the
pinned FFmpeg bundle. The portable package includes the recursive non-system
DLL dependency closure and Qt runtime plugins. PE architecture/import checks
pass; native Windows execution has not been tested. Linux x86_64 CI has not run.
The headless Linux check does not test GPU presentation, game execution, GRID0
authentication, or online matches. No Splatoon 3 FPS comparison has been made.

The second private Windows test build fixes GRID0+ private-CA registration in
cpp-httplib. A raw OpenSSL trust store did not identify the CA as explicit custom
trust, so Windows additionally checked the certificate against its own roots.
The client now declares the PEM or CA file through cpp-httplib's custom-CA API;
OpenSSL chain and hostname verification stay enabled. Credential-free TLS checks
against the configured server passed on Linux for the built-in CA and an explicit
CA file, and rejected an unrelated CA and a wrong hostname. The Windows binaries
were rebuilt; native Windows verification still needs the tester's retry.

The desktop transfer regression checks configs-only and full copies, external
NAND/save paths, custom CA and shader paths, backups, source preservation,
rejected overlaps/links, and cancellation with rollback. A headless desktop
check exercised a configs-only transfer and a fresh launch with imported values.
Manual patch status and failure dialogs were tested with a local fixture.
The ARM64 AppImage launches its bundled CLI and headless GUI on Fedora Asahi's
16 KiB page kernel, including the GRID0+ menu and transfer wizard.

The AppImage startup-process regression checks that the Vulkan probe re-enters
its launcher, inherits the probe marker, handles paths with spaces, reports exec
failures, and retains normal desktop launching without an AppImage wrapper.
A new AppImage should still be tested on the target SteamOS GPU; the ARM64 host
cannot validate RADV on the Ally.

## Release source archive

After building and committing, run:

```sh
python3 tools/release/package-source.py --output artifacts/Citrosis-source.tar.gz
```

The archive includes tracked source, pinned dependencies from `.cache/cpm`, and
file hashes. Pass it to either packager with `--source-archive` to record its
hash. Add `--release` to the Windows cross packager for normal download
instructions. Build outputs, profiles and local credentials are not included.
