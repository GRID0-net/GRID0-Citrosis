# Building Citrosis Android on the Lemon base

Repository: https://github.com/GRID0-net/GRID0-citron.
Android branch: `codex/citrosis-android` (Lemon-based).
Upstream: Ghael-V/Lemon-Project, pinned at `7fcd6ae60b74e36cce2eb06c0c39492bd636b8ec`.
The main branch retains the desktop/multiplatform source. Do not merge this entire Android tree into main.
Provenance: `citrosis-upstream.json`. Lemon history and license notices are retained.

## Build

Use JDK 17 or 21, Android SDK platform 36, NDK **28.2.13676358**, CMake **3.31.6**,
and Android build tools 36.0.0. These follow this Lemon revision's Gradle configuration;
do not reuse the old Citron NDK 26/CMake 3.22 build directory or vcpkg setup.
Lemon fetches its native dependencies through CPM during configuration. This checkout has
no Git submodules to initialize. A first build needs network access for Gradle/CPM packages.

With Android SDK command-line tools already installed:

```sh
git clone --branch codex/citrosis-android --single-branch https://github.com/GRID0-net/GRID0-citron.git GRID0-citrosis-android
cd GRID0-citrosis-android
./build-citrosis-android.sh --setup --jobs 6
```

`--setup` installs the required Android SDK components using
`$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager`; it does not install the JDK or the
command-line tools themselves. On macOS the default SDK is `~/Library/Android/sdk`; on
Linux it is `~/Android/Sdk`. Set `JAVA_HOME` (JDK 17 or 21) and `ANDROID_HOME` to override them.
The wrapper locates Homebrew JDK 21/17 directly on macOS, even without Java registry entries,
and replaces an incompatible `JAVA_HOME`. It prints the selected JDK and pins the Gradle daemon
to it. Java 27 cannot run this checkout's Gradle 8.13; the terse failure `27` was observed
with a daemon using `/opt/homebrew/Cellar/openjdk/27`.
Install the host build tools required by Lemon/CPM (Xcode command-line tools on macOS,
plus Git, Ninja, pkg-config and the relevant autotools where dependencies require them).
SDK license prompts are handled by sdkmanager when you run setup.

Later builds:

```sh
./build-citrosis-android.sh --jobs 6
./build-citrosis-android.sh --rel-debug --jobs 6
./build-citrosis-android.sh --install --jobs 6
```

The wrapper builds `assembleMainlineRelease` by default and copies the APK to
`build-android/Citrosis-release.apk`, printing its SHA-256 after your build.
`--rel-debug` selects `MainlineRelWithDebInfo`, and `--debug` selects `MainlineDebug`.
You can instead open `src/android` in Android Studio or invoke those Gradle tasks directly.
The wrapper's `--setup-only` installs SDK components without starting a build.

The mainline app is named **Citrosis**, application ID **net.grid0.citrosis**. Its source/JNI
namespace stays `dev.lemon.lemon_emu` and its native library stays `lemon-android` to retain
Lemon's JNI/resource wiring. The original Citrosis development keystore is retained; releases
signed with a personal key should use the same `ANDROID_KEYSTORE_FILE`,
`ANDROID_KEYSTORE_PASS` and `ANDROID_KEY_ALIAS` environment variables as before.
Lemon's automatic APK update checker is disabled for this downstream app.
