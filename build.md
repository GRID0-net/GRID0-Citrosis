# building Citrosis

if you just want to play, [grab a release](https://github.com/GRID0-net/GRID0-Citrosis/releases). if you want to build it yourself, heres how.

the first build downloads a bunch of dependencies, so give it some time. `--jobs 2` is a decent starting point; use `--jobs 1` if youre running low on RAM.

```sh
git clone https://github.com/GRID0-net/GRID0-Citrosis.git
cd GRID0-Citrosis
```

## Linux

these are the Fedora commands, for both x86_64 and ARM64:

```sh
sudo dnf install git clang lld cmake ninja-build gcc-c++ python3 patch \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtcharts-devel qt6-qtsvg-devel \
  SDL3-devel ffmpeg-free-devel openssl-devel zlib-devel libzstd-devel lz4-devel \
  libusb1-devel libdrm-devel glslang nasm yasm \
  alsa-lib-devel pulseaudio-libs-devel libX11-devel libXext-devel libXrandr-devel \
  libXi-devel libXcursor-devel libXinerama-devel wayland-devel libxkbcommon-devel \
  mesa-vulkan-drivers vulkan-loader squashfs-tools binutils qt6-qtwayland

bash ./build-citrosis-desktop.sh --jobs 2 --test
./build-desktop/bin/citrosis
```

other distros need the equivalent development packages. Fedora is the setup used for these builds; the AppImages are meant for running on other distros without having to build everything there.

the easy way: `build-release.sh` builds and packages in one go. with no arguments it makes the x86_64 AppImage (natively on an x86_64 PC, or cross-compiled on Fedora ARM64):

```sh
./build-release.sh                      # artifacts/Citrosis-<date>-<commit>/Citrosis-Linux-x86_64.AppImage
./build-release.sh linux-aarch64        # ARM64 AppImage (on an ARM64 machine)
./build-release.sh all --source         # both AppImages, the Windows zip and the source archive
```

every step's log goes to the output folder's `logs/`, plus `SHA256SUMS` and `BUILD-INFO.json`. `./build-release.sh --help` lists the options.

to make an AppImage by hand on the same architecture youre building on:

```sh
mkdir -p artifacts
python3 tools/linux/package-appimage.py --build build-desktop --arch "$(uname -m)" \
  --output "artifacts/Citrosis-Linux-$(uname -m).AppImage"
```

## Linux and Windows packages from a Mac

`build-release-mac.sh` runs `build-release.sh` inside a Fedora 44 container, so the same targets and options work. you need a container runtime running: [OrbStack](https://orbstack.dev), Docker Desktop, Colima or Podman.

```sh
./build-release-mac.sh all --source       # both AppImages, the Windows zip and the source archive
./build-release-mac.sh windows-x86_64     # just the Windows zip
```

on Apple Silicon the container is native ARM64 (no emulation), which is the setup `build-release.sh` cross-builds from. on an Intel Mac, `linux-aarch64` isnt available. the build happens in a container volume because macOS folders arent case-sensitive; results are copied to `artifacts/`. give the container runtime as much memory as you can spare (about 3 GB per job). `--shell` opens a shell in the build container, `--reset` deletes the cached SDKs and build folders.

## Windows

install [MSYS2](https://www.msys2.org/) and open its **CLANG64** terminal. use that terminal for all of this:

```sh
pacman -Syu
```

if it asks you to close the terminal, do that, reopen CLANG64, and run it again. then:

```sh
pacman -S --needed git make patch tar autoconf automake libtool \
  mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake \
  mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-python \
  mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-qt6-charts \
  mingw-w64-clang-x86_64-qt6-svg mingw-w64-clang-x86_64-sdl3 \
  mingw-w64-clang-x86_64-ffmpeg mingw-w64-clang-x86_64-openssl \
  mingw-w64-clang-x86_64-zlib mingw-w64-clang-x86_64-lz4 \
  mingw-w64-clang-x86_64-zstd mingw-w64-clang-x86_64-libusb \
  mingw-w64-clang-x86_64-glslang

bash ./build-citrosis-desktop.sh --jobs 2 --test
./build-desktop/bin/citrosis.exe
```

keep the DLLs and plugins with the executable when sharing a build. the [desktop build notes](docs/BUILDING-CITROSIS-DESKTOP.md) have the packaging command, plus the Fedora cross-build setup if youre building Windows from Linux.

## Android

install JDK 17 or 21 and the Android SDK command-line tools, then run:

```sh
./build-citrosis-android.sh --setup --jobs 2
```

the APK ends up at `build-android/Citrosis-release.apk`. for signing and SDK details, see the [Android build notes](docs/BUILDING-CITROSIS-ANDROID.md).

## GitHub builds

open **Actions → Citrosis desktop → Run workflow** for Windows and both Linux AppImages, or **Citrosis Android APK** for Android. download the files from the finished runs artifacts. these workflows build the files; they dont publish a release for you.

if a build fails, include the actual error and what system youre building on when reporting it. if the app runs but a game has issues, include the game version, GPU, settings, and the Citrosis log too.
