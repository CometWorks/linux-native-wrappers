#!/usr/bin/env python3
"""Verify six packaged archives and record their exact hashes; no wrappers are loaded."""
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

from package_symbols import build_id, sha256


def unpack(archive, destination):
    destination.mkdir()
    with tarfile.open(archive) as tar:
        for entry in tar:
            if not entry.isfile() or Path(entry.name).name != entry.name:
                raise ValueError(f'Unexpected archive entry: {entry.name}')
            (destination / entry.name).write_bytes(tar.extractfile(entry).read())


def verify(dist):
    archives = []
    commits = set()
    dirty_states = set()
    with tempfile.TemporaryDirectory(prefix='verify-native-release-') as temporary:
        root = Path(temporary)
        for game in ('se1', 'se2'):
            paths = {}
            for kind, suffix in (('runtime', ''), ('symbols', '.symbols'), ('debug', '.debug')):
                archive = dist / f'{game}-native-wrappers{suffix}.tar.gz'
                paths[kind] = root / (game + kind)
                unpack(archive, paths[kind])
                archives.append(dict(file=archive.name, sha256=sha256(archive), bytes=archive.stat().st_size))
            manifest = json.loads((paths['symbols'] / 'symbols.json').read_text())
            commits.add(manifest['sourceCommit'])
            dirty_states.add(manifest['sourceDirty'])
            libraries = manifest['libraries']
            assert {p.name for p in paths['runtime'].iterdir()} == {entry['file'] for entry in libraries}
            assert {p.name for p in paths['debug'].iterdir()} == {entry['file'] for entry in libraries}
            assert {p.name for p in paths['symbols'].iterdir()} == {'symbols.json'} | {e['symbols'] for e in libraries}
            for entry in libraries:
                runtime = paths['runtime'] / entry['file']
                symbols = paths['symbols'] / entry['symbols']
                assert sha256(runtime) == entry['binarySha256'], runtime
                assert sha256(symbols) == entry['symbolsSha256'], symbols
                assert build_id(runtime) == build_id(symbols) == entry['buildId'], runtime
                sections = subprocess.check_output(['readelf', '-SW', str(runtime)], text=True)
                assert '.debug_info' not in sections and '.gnu_debuglink' in sections, runtime
                for binary in (symbols, paths['debug'] / entry['file']):
                    assert '.debug_info' in subprocess.check_output(['readelf', '-SW', str(binary)], text=True), binary
    assert len(commits) == 1, 'SE1 and SE2 symbols came from different source commits'
    assert len(dirty_states) == 1, 'SE1 and SE2 source states differ'
    result = dict(schemaVersion=1, sourceCommit=commits.pop(), sourceDirty=dirty_states.pop(), artifacts=archives)
    (dist / 'release-manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Verified six archives: shipped hashes, matching build IDs, Release split symbols and Debug DWARF')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else 'dist'))
