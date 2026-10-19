#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Extract an x86_64 SDK locally on a Fedora ARM64 host; no system installation.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
mkdir -p .cache/linux-x86_64-rpms .cache/linux-x86_64-sdk
dnf --forcearch=x86_64 --repo=fedora --repo=updates --setopt=logdir=/tmp download \
  --arch=x86_64 --resolve --alldeps --destdir=.cache/linux-x86_64-rpms \
  gcc-c++ glibc-devel qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtcharts-devel \
  qt6-qtsvg-devel qt6-qtwayland SDL3-devel ffmpeg-free-devel openssl-devel \
  zlib-devel libzstd-devel lz4-devel libusb1-devel \
  alsa-lib-devel pulseaudio-libs-devel libdrm-devel wayland-devel libxkbcommon-devel
python3 - <<'PY'
from pathlib import Path
import subprocess
sdk = Path('.cache/linux-x86_64-sdk').resolve()
for rpm in sorted(Path('.cache/linux-x86_64-rpms').glob('*.rpm')):
    if not rpm.name.endswith(('.x86_64.rpm', '.noarch.rpm')) or rpm.name.startswith(('filesystem-', 'basesystem-')):
        continue
    for directory in (sdk, sdk / 'usr', sdk / 'usr/lib', sdk / 'usr/lib64', sdk / 'usr/bin'):
        if directory.is_dir() and not directory.is_symlink(): directory.chmod(directory.stat().st_mode | 0o700)
    process = subprocess.Popen(['rpm2cpio', str(rpm.resolve())], stdout=subprocess.PIPE)
    subprocess.run(['cpio', '-idmu', '--quiet', '--no-absolute-filenames'], stdin=process.stdout, cwd=sdk, check=True)
    process.stdout.close()
    if process.wait():
        raise SystemExit('RPM extraction failed: ' + str(rpm))
for name, target in (('lib64', 'usr/lib64'), ('lib', 'usr/lib'), ('bin', 'usr/bin'), ('sbin', 'usr/sbin')):
    path = sdk / name
    if not path.exists() and not path.is_symlink(): path.symlink_to(target)
# Some exported CMake targets contain literal system paths.
for path in (sdk / 'usr').rglob('*.cmake'):
    text = path.read_text()
    for prefix in ('/usr/lib64/', '/usr/include/'):
        text = text.replace('"' + prefix, '"' + str(sdk) + prefix)
    path.write_text(text)
PY
