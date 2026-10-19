#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bundle a release build, its ELF dependency closure, Qt plugins and a pinned Type 2 runtime."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, urllib.request
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--arch', choices=('aarch64', 'x86_64'), required=True)
parser.add_argument('--sysroot', type=pathlib.Path, default=pathlib.Path('/'))
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--source-archive', type=pathlib.Path)
args = parser.parse_args()
source = pathlib.Path(__file__).resolve().parents[2]
build, sysroot, output = args.build.resolve(), args.sysroot.resolve(), args.output.resolve()
cache = (build / 'CMakeCache.txt').read_text()
if not re.search(r'^CMAKE_BUILD_TYPE:[^=]*=Release$', cache, re.M): raise SystemExit('AppImages require a Release build')
if output.exists(): raise SystemExit(f'Refusing to overwrite {output}')
appdir = output.parent / (output.stem + '.AppDir')
if appdir.exists(): raise SystemExit(f'Refusing to overwrite {appdir}')
sha = {'aarch64': '00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444',
       'x86_64': '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d'}[args.arch]
runtime = source / '.cache/appimage' / ('runtime-' + args.arch + '-20251108')
runtime.parent.mkdir(parents=True, exist_ok=True)
if not runtime.exists():
    urllib.request.urlretrieve('https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-' + args.arch, runtime)
if hashlib.sha256(runtime.read_bytes()).hexdigest() != sha: raise SystemExit('AppImage runtime SHA256 mismatch')
readelf = shutil.which('llvm-readelf') or shutil.which('readelf')
if not readelf: raise SystemExit('readelf required')
# Resolve absolute symlinks inside a cross sysroot, never into the host libraries.
def resolve(path):
    for _ in range(40):
        if not path.is_symlink(): return path
        link = pathlib.Path(os.readlink(path))
        path = (link if str(link).startswith(str(sysroot) + '/') else sysroot / str(link).lstrip('/')) if link.is_absolute() else path.parent / link
    raise RuntimeError('Symlink loop: ' + str(path))
search = [sysroot / p for p in ('usr/lib64', 'lib64', 'usr/lib', 'lib', 'usr/lib64/pulseaudio', 'usr/lib64/qt6/lib')]
def library(name):
    for directory in search:
        p = resolve(directory / name)
        if p.is_file(): return p
    for parent in (sysroot / 'usr/lib64', sysroot / 'usr/lib'):
        for p in sorted(parent.rglob(name)):
            p = resolve(p)
            if p.is_file(): return p
    raise RuntimeError('Missing ELF dependency: ' + name)
for p in ('usr/bin', 'usr/lib', 'usr/plugins', 'usr/share/licenses'):
    (appdir / p).mkdir(parents=True, exist_ok=True)
queue, visited = [], set()
for name in ('citrosis', 'citrosis-cli', 'citrosis-room'):
    p = build / 'bin' / name
    if not p.is_file(): raise SystemExit('Missing executable: ' + str(p))
    shutil.copy2(p, appdir / 'usr/bin' / name)
    queue.append(appdir / 'usr/bin' / name)
plugins = sysroot / 'usr/lib64/qt6/plugins'
for category in ('platforms', 'imageformats', 'iconengines', 'tls', 'networkinformation',
                 'platforminputcontexts', 'xcbglintegrations', 'wayland-graphics-integration-client',
                 'wayland-decoration-client', 'wayland-shell-integration'):
    for p in (plugins / category).glob('*.so'):
        if category == 'imageformats' and p.name not in ('libqgif.so', 'libqico.so', 'libqjpeg.so', 'libqsvg.so'): continue
        if category == 'iconengines' and p.name != 'libqsvgicon.so': continue
        if category == 'platforminputcontexts' and not any(s in p.name for s in ('ibus', 'compose')): continue
        if category == 'platforms' and not any(n in p.name for n in ('xcb', 'wayland', 'offscreen', 'minimal.')): continue
        dest = appdir / 'usr/plugins' / category / p.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dest)
        queue.append(dest)
for p in (sysroot / 'usr/lib64/ossl-modules').glob('*.so'):
    dest = appdir / 'usr/lib/ossl-modules' / p.name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, dest)
    queue.append(dest)
if not (appdir / 'usr/plugins/platforms/libqxcb.so').exists(): raise SystemExit('Qt XCB plugin missing')
while queue:
    p = queue.pop()
    key = str(p.relative_to(appdir))
    if key in visited: continue
    visited.add(key)
    headers = subprocess.check_output([readelf, '-h', '-d', str(p)], text=True)
    machine = 'AArch64' if args.arch == 'aarch64' else 'Advanced Micro Devices X86-64'
    if machine not in headers and not (args.arch == 'x86_64' and 'x86-64' in headers.lower()):
        raise RuntimeError('Wrong ELF architecture: ' + str(p))
    for name in re.findall(r'\(NEEDED\).*?\[(.*?)\]', headers):
        target = appdir / 'usr/lib' / name
        if not target.exists():
            shutil.copy2(library(name), target)
            queue.append(target)
loader = 'ld-linux-aarch64.so.1' if args.arch == 'aarch64' else 'ld-linux-x86-64.so.2'
# The bundled loader sits beside the executables so Qt's applicationDirPath,
# even when obtained through /proc/self/exe, still resolves the correct qt.conf.
shutil.copy2(library(loader), appdir / 'usr/bin' / loader)
(appdir / 'usr/bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\n')
cert = next((p for p in (sysroot / 'etc/pki/tls/certs/ca-bundle.crt', pathlib.Path('/etc/pki/tls/certs/ca-bundle.crt'), pathlib.Path('/etc/ssl/certs/ca-certificates.crt')) if p.is_file()), None)
if not cert: raise RuntimeError('Missing public CA bundle')
shutil.copy2(cert, appdir / 'usr/share/ca-bundle.crt')
(appdir / 'AppRun').write_text('''#!/bin/sh
set -eu
APPDIR=${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
export APPDIR
# The Vulkan startup probe must use the same loader/library setup as the GUI.
CITROSIS_APPIMAGE_LAUNCHER="$APPDIR/AppRun"
export CITROSIS_APPIMAGE_LAUNCHER
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
OPENSSL_MODULES=${OPENSSL_MODULES:-$APPDIR/usr/lib/ossl-modules}
export OPENSSL_MODULES
exe=citrosis
case "${1:-}" in
  --cli) exe=citrosis-cli; shift ;;
  --room) exe=citrosis-room; shift ;;
esac
# Prefer the current host trust bundle when present.
if [ -z "${SSL_CERT_FILE:-}" ]; then
  SSL_CERT_FILE="$APPDIR/usr/share/ca-bundle.crt"
  for cert in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt; do
    if [ -f "$cert" ]; then SSL_CERT_FILE="$cert"; break; fi
  done
  export SSL_CERT_FILE
fi
exec "$APPDIR/usr/bin/''' + loader + '''" --library-path "$APPDIR/usr/lib" "$APPDIR/usr/bin/$exe" "$@"
''')
(appdir / 'AppRun').chmod(0o755)
shutil.copy2(source / 'dist/net.grid0.citrosis.desktop', appdir)
shutil.copy2(source / 'dist/net.grid0.citrosis.svg', appdir)
shutil.copy2(source / 'dist/net.grid0.citrosis.svg', appdir / '.DirIcon')
shutil.copytree(source / 'LICENSES', appdir / 'usr/share/licenses/citrosis')
shutil.copy2(source / 'LICENSE.txt', appdir / 'usr/share/licenses/Citrosis.txt')
for p in (sysroot / 'usr/share/licenses').iterdir():
    # Keep library notices from the SDK; no settings or home directories are copied.
    if p.is_dir(): shutil.copytree(p, appdir / 'usr/share/licenses/Fedora' / p.name, symlinks=True)
license_file = source / '.cache/appimage/runtime-LICENSE-20251108'
if not license_file.exists():
    urllib.request.urlretrieve('https://raw.githubusercontent.com/AppImage/type2-runtime/dd6cebe/LICENSE', license_file)
if hashlib.sha256(license_file.read_bytes()).hexdigest() != 'aa154fc9070614bbe7921f89db11efd1dba7a1f3a41685958110e2230f9c0ca1': raise RuntimeError('Runtime license checksum mismatch')
shutil.copy2(license_file, appdir / 'usr/share/licenses/AppImage-runtime.txt')
shutil.copy2(source / 'citrosis-desktop-port.json', appdir)
shutil.copy2(source / 'docs/BUILDING-CITROSIS-DESKTOP.md', appdir)
for name in ('README.md', 'build.md'):
    shutil.copy2(source / name, appdir)
manifest = {'arch': args.arch, 'build_type': 'Release', 'elf_images_checked': len(visited),
            'runtime_sha256': sha, 'files': {}}
if args.source_archive:
    manifest['source_archive'] = {'name': args.source_archive.name, 'sha256': hashlib.sha256(args.source_archive.read_bytes()).hexdigest()}
for p in sorted(appdir.rglob('*')):
    if p.is_file() and not p.is_symlink(): manifest['files'][str(p.relative_to(appdir))] = hashlib.sha256(p.read_bytes()).hexdigest()
(appdir / 'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
squashfs = output.with_suffix('.squashfs')
subprocess.run(['mksquashfs', str(appdir), str(squashfs), '-noappend', '-all-root', '-no-xattrs',
                '-comp', 'zstd', '-processors', '2'], check=True, stdout=subprocess.DEVNULL)
with output.open('wb') as out:
    for p in (runtime, squashfs):
        with p.open('rb') as inp: shutil.copyfileobj(inp, out)
output.chmod(0o755)
squashfs.unlink()
(output.with_suffix('.AppImage.sha256')).write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
print(f'{output}: {len(visited)} ELF images checked; all dependencies bundled.')
