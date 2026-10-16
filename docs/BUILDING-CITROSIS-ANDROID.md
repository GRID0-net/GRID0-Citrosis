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

## GRID0+ and LAN

Open Settings → Online. It contains:

- GRID0+ username/password, login server, game redirect address, secondary NAT address and CA bundle.
- **Test GRID0+ login**, which validates the configured GRID0+ account.
- **GRID0+ Friends**, with friend code, presence, incoming/outgoing requests and friend actions.
- Network interface selection for Wi-Fi, mobile and VPN/overlay adapters. Android supplies
  gateways from ConnectivityManager; a Wi-Fi multicast lock is held during emulation.

GRID0+ is the only integrated online account provider; Nextendo has been removed.
An empty private-server address with no GRID0+ account configured disables GRID0+ redirection.
The migrated GRID0+ default addresses match the original checkout's settings.

GRID0+ includes signed account tokens/account IDs, DNS redirection, CA trust, friends/presence,
BCAT downloads, eventfd/deferred socket polling, Pia socket options and exact-build toolbox
patches. The shipped Splatoon 3 patches match build ID
`28C4287AEE36F7499DA60F3E68B54C70DA382D75`; other builds do not receive those patches.
No account credentials, game dumps, firmware or user keys were copied into this checkout.

## Memory controls and HDR

Settings → Performance exposes **Reclaim guest memory** and **Fit caches to RAM**.
Defaults are on. The native monitor reports budgets, MemAvailable/RSS, pressure changes and swap.
On a device reporting 7233 MiB, base texture targets are approximately 289/434 MiB and buffer
101/173 MiB; moderate pressure scales these to 75%, critical pressure to 50%.
They are GC targets rather than hard process-RSS limits.

Guest zero fills use sparse memfd holes; freed guest pages are discarded, and optimized guest
allocations coalesce new pages instead of touching each page. The staging ring is 16 MiB.
Completed staging buffers are reclaimed above the pressure/warm targets; over 256 MiB or in
an emergency, frame-boundary completion provides backpressure. Deferred and in-flight buffers
remain protected and can exceed targets. Extra mid-draw submissions were not imported because
Lemon's uniform/index lifetimes differ from the old renderer. Async texture decoding is bounded
at four outstanding jobs with at least 512 MiB available; otherwise uploads use synchronous conversion.

The Splatoon 3 HDR fix preserves the known float-upload NaN/Inf-to-zero rules, including packed
B10G11R11 float masks, with ARM64 NEON and scalar tails. Cleaning happens before mapped Vulkan
writes using small stack temporaries, avoiding the expensive mapped-buffer rescan. Logs use
`Citrosis HDR: scrubbed ...`. `CITROSIS_TC_SANITIZE=0` remains a diagnostic override.
The original NCE signal-handler experiments and their launch-regression follow-up were not imported.

## Validation ownership

No Gradle/CMake configure, compilation, APK build, tests or device runs were performed during
this migration. Source review, local include/resource-reference inspection, XML parsing,
formatting and `git diff --check` are the available checks, not build/gameplay proof.

Your first run should verify launch, HDR/ink/reflections, sustained RAM/swap, GRID0+ login,
friend-list actions, BCAT and a real two-player match with the same map/camera route. Keep the
full log and distinguish shader first-use waits from warm match FPS. Lemon being the new base
is not proof of a particular performance or correctness improvement on Adreno 660.


## GitHub Actions

After the Android workflow integration is merged into main, open **Actions →
Citrosis Android APK → Run workflow**. Leave the workflow branch on main and the
`source_ref` input on `codex/citrosis-android`, or use an Android commit/tag for a
repeatable source selection. The workflow explicitly checks out that source and
rejects a desktop tree. It does not run automatically on pushes, create tags or
publish GitHub releases. The existing desktop Release workflow is independent.

The uploaded artifact includes `Citrosis-release.apk`, `SHA256SUMS`,
`SOURCE_COMMIT.txt` and `citrosis-upstream.json`. The source commit records the
Android revision actually built, not the main revision containing the workflow.
Builds use JDK 21 and the wrapper's pinned SDK/NDK/CMake versions with two native
jobs. Default CI signing uses the existing development keystore; use your own
release key for production distribution.

The user reported the latest build working on 2026-10-16 following the ssl:s and
NIFM fixes. The new GitHub Actions workflow has not been run as part of publication.
