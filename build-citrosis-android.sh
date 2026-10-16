#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Citrosis contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
variant=Release
setup=0
setup_only=0
install_apk=0
jobs=""
while (($#)); do
    case "$1" in
        --setup) setup=1 ;;
        --setup-only) setup=1; setup_only=1 ;;
        --debug) variant=Debug ;;
        --rel-debug) variant=RelWithDebInfo ;;
        --install) install_apk=1 ;;
        --jobs) shift; jobs=${1:?--jobs requires a number} ;;
        --help|-h)
            echo 'Usage: ./build-citrosis-android.sh [--setup|--setup-only] [--debug|--rel-debug] [--jobs N] [--install]'
            echo 'Uses Lemon mainline, NDK 28.2.13676358, CMake 3.31.6, Android 36 and JDK 17 or 21.'
            echo '--setup installs SDK components using an existing Android SDK command-line tools installation.'
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
# Gradle 8.13 cannot run on newer host JDKs such as Java 27. Homebrew's
# versioned JDKs need not be registered with /usr/libexec/java_home.
supported_jdk() {
    local candidate=$1 version
    [[ -x "$candidate/bin/java" && -f "$candidate/release" ]] || return 1
    version=$(sed -n 's/^JAVA_VERSION="\([^".]*\).*"/\1/p' "$candidate/release")
    [[ $version == 17 || $version == 21 ]]
}
selected_jdk=""
if [[ -n ${JAVA_HOME:-} ]] && supported_jdk "$JAVA_HOME"; then
    selected_jdk=$JAVA_HOME
fi
if [[ -z $selected_jdk && $(uname -s) == Darwin ]]; then
    for major in 21 17; do
        for candidate in "/opt/homebrew/opt/openjdk@$major/libexec/openjdk.jdk/Contents/Home" "/usr/local/opt/openjdk@$major/libexec/openjdk.jdk/Contents/Home"; do
            if supported_jdk "$candidate"; then selected_jdk=$candidate; break; fi
        done
        [[ -n $selected_jdk ]] && break
        candidate=$(/usr/libexec/java_home -v "$major" 2>/dev/null || true)
        if supported_jdk "$candidate"; then selected_jdk=$candidate; break; fi
    done
fi
if [[ -z $selected_jdk ]]; then
    echo 'Install JDK 17 or 21 and set JAVA_HOME to that JDK. This wrapper does not use newer JDKs with Gradle 8.13.' >&2
    exit 1
fi
if [[ -n ${JAVA_HOME:-} && $JAVA_HOME != "$selected_jdk" ]]; then
    echo "Ignoring incompatible JAVA_HOME: $JAVA_HOME" >&2
fi
export JAVA_HOME="$selected_jdk"
export PATH="$JAVA_HOME/bin:$PATH"
echo "JDK: $JAVA_HOME"
"$JAVA_HOME/bin/java" -version
if [[ -z ${ANDROID_HOME:-} ]]; then
    if [[ $(uname -s) == Darwin ]]; then ANDROID_HOME="$HOME/Library/Android/sdk";
    else ANDROID_HOME="$HOME/Android/Sdk"; fi
fi
export ANDROID_HOME ANDROID_SDK_ROOT="$ANDROID_HOME"
if ((setup)); then
    sdkmanager="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
    [[ -x "$sdkmanager" ]] || { echo "Install Android SDK command-line tools in $ANDROID_HOME/cmdline-tools/latest first." >&2; exit 1; }
    "$sdkmanager" --sdk_root="$ANDROID_HOME" 'platform-tools' 'platforms;android-36' 'build-tools;36.0.0' 'ndk;28.2.13676358' 'cmake;3.31.6'
fi
((setup_only)) && exit 0
[[ -d "$ANDROID_HOME/ndk/28.2.13676358" ]] || { echo 'Missing NDK 28.2.13676358; run with --setup.' >&2; exit 1; }
# Do not let a previous desktop/cross-build environment select the wrong host compiler.
unset CC CXX CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
if [[ -n $jobs ]]; then
    [[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be a positive integer.' >&2; exit 2; }
    export CMAKE_BUILD_PARALLEL_LEVEL="$jobs"
fi
cd "$root/src/android"
gradle_args=("assembleMainline$variant" --console=plain "-Dorg.gradle.java.home=$JAVA_HOME")
[[ -n $jobs ]] && gradle_args+=("--max-workers=$jobs")
./gradlew "${gradle_args[@]}"
variant_dir=$(printf '%s' "$variant" | tr '[:upper:]' '[:lower:]')
# AGP uses lower camel case for relWithDebInfo directories.
[[ $variant == RelWithDebInfo ]] && variant_dir=relWithDebInfo
apk_dir="$root/src/android/app/build/outputs/apk/mainline/$variant_dir"
apk=""
for candidate in "$apk_dir"/*.apk; do
    [[ -f $candidate ]] && apk="$candidate"
done
[[ -n $apk ]] || { echo "No APK found in $apk_dir" >&2; exit 1; }
mkdir -p "$root/build-android"
output="$root/build-android/Citrosis-${variant_dir}.apk"
cp "$apk" "$output"
echo "APK: $output"
if command -v shasum >/dev/null; then shasum -a 256 "$output"; else sha256sum "$output"; fi
if ((install_apk)); then "$ANDROID_HOME/platform-tools/adb" install -r "$output"; fi
