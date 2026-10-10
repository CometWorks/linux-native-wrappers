#!/usr/bin/env python3
"""Split debug sections from staged Release copies; preserve the original build outputs."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def build_id(path):
    notes = subprocess.check_output(['readelf', '-n', str(path)], text=True)
    match = re.search(r'Build ID: ([0-9a-f]+)', notes)
    if not match:
        raise ValueError(f'{path} has no ELF build ID')
    return match.group(1)


def package(staging, archive, libraries):
    archive.parent.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).strip())
    with tempfile.TemporaryDirectory(prefix='symbols-', dir=archive.parent) as temporary:
        symbols = Path(temporary)
        entries = []
        for name in libraries:
            if Path(name).name != name or not name.endswith('.so'):
                raise ValueError(f'Expected a library basename, got {name!r}')
            binary = staging / name
            identity = build_id(binary)
            sections = subprocess.check_output(['readelf', '-SW', str(binary)], text=True)
            if '.debug_info' not in sections:
                raise ValueError(f'{binary} has no DWARF; compile Release with -g')
            debug = symbols / (name + '.debug')
            subprocess.run(['objcopy', '--only-keep-debug', str(binary), str(debug)], check=True)
            subprocess.run(['strip', '--strip-debug', str(binary)], check=True)
            subprocess.run(['objcopy', '--add-gnu-debuglink=' + str(debug), str(binary)], check=True)
            if build_id(binary) != identity or build_id(debug) != identity:
                raise ValueError(f'{name}: splitting symbols changed the ELF build ID')
            entries.append(dict(file=name, binarySha256=sha256(binary), buildId=identity,
                                symbols=debug.name, symbolsSha256=sha256(debug)))
        (symbols / 'symbols.json').write_text(json.dumps(dict(schemaVersion=1, configuration='Release',
            sourceCommit=commit, sourceDirty=dirty, libraries=entries), indent=2) + '\n')
        # Fixed gzip and tar metadata preserve the runtime archive's reproducibility convention.
        with archive.open('wb') as output, gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as tar:
                for path in sorted(symbols.iterdir()):
                    info = tar.gettarinfo(str(path), arcname=path.name)
                    info.uid = info.gid = 0
                    info.uname = info.gname = ''
                    info.mode = 0o644
                    info.mtime = 1577836800
                    with path.open('rb') as source:
                        tar.addfile(info, source)


if __name__ == '__main__':
    if len(sys.argv) < 4:
        raise SystemExit('Usage: package_symbols.py STAGING ARCHIVE LIBRARY.so [...]')
    package(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:])
