#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive tracked source and downloaded dependency sources for a release."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = args.output.resolve()
if output.exists():
    raise SystemExit(f'Refusing to overwrite {output}')
dependencies = root / '.cache/cpm'
if not dependencies.is_dir():
    raise SystemExit('Build first so .cache/cpm contains the pinned dependencies.')
output.parent.mkdir(parents=True, exist_ok=True)
prefix = output.name.removesuffix('.tar.gz')
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root))
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).split(b'\0')
manifest = {'source_commit': commit, 'local_changes_included': dirty, 'files': {}}
with tarfile.open(output, 'w:gz', compresslevel=1) as archive:
    for raw in sorted(set(files)):
        if not raw:
            continue
        name = raw.decode()
        path = root / name
        if path.is_file():
            archive.add(path, arcname=prefix + '/' + name, recursive=False)
            manifest['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
    archive.add(dependencies, arcname=prefix + '/.cache/cpm',
                filter=lambda member: None if '.git' in Path(member.name).parts else member)
    for name, text in {
        'GIT-COMMIT': commit + '\n',
        'SOURCE-STATE.json': json.dumps(manifest, indent=2) + '\n',
        'README-SOURCE.txt': 'Citrosis release source. See build.md for build instructions.\n'
                             'Pinned dependency sources are included under .cache/cpm.\n'
                             'SOURCE-STATE.json records the exact source file hashes.\n',
    }.items():
        data = text.encode()
        info = tarfile.TarInfo(prefix + '/' + name)
        info.size, info.mode = len(data), 0o644
        archive.addfile(info, io.BytesIO(data))
print(output)
print('SHA256:', hashlib.sha256(output.read_bytes()).hexdigest())
