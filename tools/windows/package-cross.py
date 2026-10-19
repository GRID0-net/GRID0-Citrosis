#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage a portable Windows x64 build on Linux, verifying its PE import closure."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--sdk', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--source-archive', type=pathlib.Path)
parser.add_argument('--release', action='store_true', help='Use release-facing instructions instead of private-test notes')
args = parser.parse_args()
source = pathlib.Path(__file__).resolve().parents[2]
build, sdk, output = (p.resolve() for p in (args.build, args.sdk, args.output))
if output.exists():
    raise SystemExit(f'Refusing to overwrite existing package: {output}')
mingw = sdk / 'usr/x86_64-w64-mingw32/sys-root/mingw'
objdump = shutil.which('llvm-objdump')
if not objdump:
    raise SystemExit('llvm-objdump is required')
# Only DLLs supplied by supported Windows installations may be omitted.
system = set('''advapi32 authz avrt bcrypt bcryptprimitives cfgmgr32 comctl32 comdlg32
crypt32 cryptbase cryptnet cryptsp d2d1 d3d11 d3d12 d3d9 dcomp debughelp dbghelp
dinput8 dnsapi dwmapi dwrite dxgi dxva2 fwpuclnt gdi32 glu32 hid imm32 iphlpapi
kernel32 kernelbase mf mfplat mfreadwrite mfuuid mmdevapi mpr msacm32 msvcrt
msvcp_win mswsock ncrypt netapi32 normaliz ntdll ole32 oleacc oleaut32 opengl32
powrprof propsys psapi rasapi32 rpcrt4 secur32 setupapi shcore shell32 shlwapi user32
userenv usp10 ucrtbase uxtheme version win32u windowscodecs winhttp wininet
winmm winscard winspool wintrust wlanapi wldap32 ws2_32 wtsapi32'''.split())
system = {name + '.dll' for name in system}
search = [build / 'bin', mingw / 'bin']
# Bundled dependencies may also ship runtime DLLs.
ffmpeg = source / '.cache/cpm/ffmpeg'
search.extend(p for p in ffmpeg.glob('*mingw-amd64/bin') if p.is_dir())
dlls = {}
for directory in search:
    for path in directory.glob('*'):
        if path.suffix.lower() == '.dll':
            key = path.name.lower()
            if key in dlls and hashlib.sha256(path.read_bytes()).digest() != hashlib.sha256(dlls[key].read_bytes()).digest():
                raise SystemExit(f'Conflicting DLLs: {dlls[key]}, {path}')
            dlls[key] = path
output.mkdir(parents=True)
queue = []
for name in ('citrosis', 'citrosis-cli', 'citrosis-room'):
    exe = build / 'bin' / (name + '.exe')
    if not exe.is_file():
        raise SystemExit(f'Missing executable: {exe}')
    queue.append(pathlib.Path(shutil.copy2(exe, output)))
plugins = mingw / 'lib/qt6/plugins'
# Runtime-selected plugins must be explicit roots of dependency traversal.
for category in ('platforms', 'styles', 'imageformats', 'iconengines', 'tls', 'networkinformation'):
    for plugin in (plugins / category).glob('*.dll'):
        if category == 'platforms' and plugin.name != 'qwindows.dll':
            continue
        destination = output / 'plugins' / category
        destination.mkdir(parents=True, exist_ok=True)
        queue.append(pathlib.Path(shutil.copy2(plugin, destination)))
if not (output / 'plugins/platforms/qwindows.dll').is_file():
    raise SystemExit('Missing Qt Windows platform plugin')
seen, omitted = set(), set()
while queue:
    pe = queue.pop()
    key = pe.name.lower()
    if key in seen:
        continue
    seen.add(key)
    headers = subprocess.check_output([objdump, '-p', str(pe)], text=True)
    if 'coff-x86-64' not in headers:
        raise SystemExit(f'Not a Windows x64 PE image: {pe}')
    for dependency in re.findall(r'DLL Name:\s*(\S+)', headers):
        dependency = dependency.lower()
        if dependency in system or dependency.startswith(('api-ms-win-', 'ext-ms-win-')):
            omitted.add(dependency)
            continue
        if dependency in seen:
            continue
        if dependency not in dlls:
            raise SystemExit(f'Unresolved DLL {dependency} imported by {pe.name}')
        queue.append(pathlib.Path(shutil.copy2(dlls[dependency], output)))
(output / 'qt.conf').write_text('[Paths]\nPlugins=plugins\n', encoding='utf-8')
for name in ('LICENSES',):
    shutil.copytree(source / name, output / name)
for name in ('LICENSE.txt', 'citrosis-desktop-port.json'):
    shutil.copy2(source / name, output)
license_root = sdk / 'usr/share/licenses'
for directory in license_root.glob('mingw*'):
    shutil.copytree(directory, output / 'LICENSES/Fedora-MinGW' / directory.name)
for directory in mingw.glob('share/licenses/*'):
    if directory.is_dir():
        shutil.copytree(directory, output / 'LICENSES/MinGW' / directory.name)
for package in (source / '.cache/cpm').iterdir():
    if not package.is_dir():
        continue
    for version in package.iterdir():
        if not version.is_dir():
            continue
        for notice in version.iterdir():
            if notice.is_file() and notice.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'COPYRIGHT')):
                target = output / 'LICENSES/CPM' / package.name / version.name
                target.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, target)
for name in ('post_shaders', 'post_presets'):
    shutil.copytree(source / 'dist' / name, output / 'extras' / name)
shutil.copy2(source / 'docs/BUILDING-CITROSIS-DESKTOP.md', output)
readme = '''Citrosis - private Windows x64 test build

Extract the entire ZIP, then launch citrosis.exe. Keep all DLLs and plugins
beside it. Windows 10/11 x64 and a Vulkan-capable GPU/driver are required.
This is a Release (-O2) build of the current Android-derived Citrosis core
with the restored desktop frontend, GRID0+ account controls and friends.

Use your own game dumps, keys and firmware. None are included. This ZIP
includes a fresh portable user folder. Keep it beside the EXE: settings,
logs, keys, firmware and saves will be stored there. Removing that folder
switches the application to %APPDATA%\\citrosis.

Emulation > Configure contains graphics, memory and frame generation settings.
GRID0+ > Settings opens network account settings. GRID0+ > Friends provides
friend requests and search by friend code. Tools > Transfer from Eden opens
the desktop transfer wizard. The updater is disabled for this private build.

This package was cross-compiled on Fedora ARM64. PE architecture and all
non-system DLL imports were checked. It has not been launched on native
Windows; game performance and real GRID0+ multiplayer need testing.

For a useful test report: GPU and driver version, game/update version,
settings, what happened, and the log file from user/log (portable mode).
The source snapshot and dependency lock/provenance accompany this build.
'''
if args.release:
    readme = """Citrosis for Windows

extract the entire zip, then open citrosis.exe. keep the DLLs and plugins with it.
this build is for Windows 10/11 x64.

already using Eden? Tools > Transfer from Eden can copy your full user folder,
or just your configs and paths. GRID0+ has its own menu for settings, friends
and patch updates. graphics and controller settings are in Emulation > Configure.

bring your own keys, firmware and game dumps. none are included.
the user folder beside the exe keeps this build portable. keep your existing
user folder when updating; dont delete it or replace it with someone elses.

see README.md and build.md for more info. BUILD-MANIFEST.json records the build
and the matching source archive. this package was cross-compiled on Fedora;
native Windows game testing is still needed.
"""
(output / ('START-HERE.txt' if args.release else 'README-TEST.txt')).write_text(readme, encoding='utf-8')
for name in ('README.md', 'build.md'):
    shutil.copy2(source / name, output)
(output / 'user').mkdir()
(output / 'user/README.txt').write_text(
    'Portable profile: Citrosis stores settings, keys, saves and logs here.\n',
    encoding='utf-8')
commit_file = source / 'GIT-COMMIT'
core_base_commit = commit_file.read_text().strip() if commit_file.exists() else subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
manifest = {
    'platform': 'windows-x86_64', 'build_type': 'Release',
    'core_base_commit': core_base_commit,
    'local_changes_included': True,
    'native_windows_runtime_tested': False,
    'pe_images_checked': len(seen), 'windows_system_imports': sorted(omitted),
    'files': {},
}
if args.source_archive:
    manifest['source_archive'] = {
        'name': args.source_archive.name,
        'sha256': hashlib.sha256(args.source_archive.read_bytes()).hexdigest(),
    }
manifest['sdk_rpms'] = [
    {'name': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    for p in sorted((source / '.cache/windows-rpms').glob('mingw*.rpm'))
]
for path in sorted(output.rglob('*')):
    if path.is_file():
        manifest['files'][str(path.relative_to(output))] = hashlib.sha256(path.read_bytes()).hexdigest()
(output / 'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
archive = output.with_suffix('.zip')
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for path in sorted(output.rglob('*')):
        if path.is_file():
            z.write(path, pathlib.Path(output.name) / path.relative_to(output))
print(f'Checked {len(seen)} Windows x64 PE images; all non-system imports resolved.')
print(archive)
print('SHA256:', hashlib.sha256(archive.read_bytes()).hexdigest())
