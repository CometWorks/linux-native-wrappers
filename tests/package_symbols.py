#!/usr/bin/env python3
"""Exercise Release symbol packaging with a tiny ELF, without building any wrappers."""
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from elf_exports import rewrite_exports


def output(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


with tempfile.TemporaryDirectory(prefix='native-symbols-test-') as temporary:
    root = Path(temporary)
    source = root / 'fixture.cpp'
    source.write_text('''extern "C" int fixture(int x) { return x * 3 + 7; }
extern "C" int decorated(int) __asm__("fixture$decorated");
extern "C" int decorated(int x) { return x * 5 + 2; }
''')
    original = root / 'libfixture.so'
    subprocess.run(['g++', '-shared', '-fPIC', '-O3', '-g', '-Wl,--build-id=sha1',
                    '-Wl,--hash-style=sysv', str(source), '-o', str(original)], check=True)
    # Physics/Slug patch dynamic names after linking, so exercise the same path.
    assert rewrite_exports(original, lambda name: name.replace(b'$', b'@')
                           if b'$' in name else None) == 1
    original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
    archives = []
    for number in range(2):
        staging = root / str(number)
        staging.mkdir()
        runtime = staging / original.name
        shutil.copy2(original, runtime)
        archive = root / f'{number}.symbols.tar.gz'
        subprocess.run([sys.executable, str(ROOT / 'tools/package_symbols.py'),
                        str(staging), str(archive), runtime.name], check=True)
        archives.append(archive.read_bytes())
        assert '.debug_info' not in output('readelf', '-SW', runtime)
        assert '.gnu_debuglink' in output('readelf', '-SW', runtime)
        for section in ('.text', '.dynstr', '.dynsym', '.hash'):
            assert output('readelf', '-x', section, runtime) == output('readelf', '-x', section, original)
        library = ctypes.CDLL(str(runtime))
        assert library.fixture(11) == 40
        assert getattr(library, 'fixture@decorated')(11) == 57
        with tarfile.open(archive) as tar:
            assert sorted(tar.getnames()) == ['libfixture.so.debug', 'symbols.json']
            debug_bytes = tar.extractfile('libfixture.so.debug').read()
            manifest = json.load(tar.extractfile('symbols.json'))
        debug = staging / 'libfixture.so.debug'
        debug.write_bytes(debug_bytes)
        assert '.debug_info' in output('readelf', '-SW', debug)
        assert manifest['configuration'] == 'Release'
        assert manifest['schemaVersion'] == 1
        assert manifest['sourceCommit'] == output('git', '-C', ROOT, 'rev-parse', 'HEAD').strip()
        entry, = manifest['libraries']
        assert entry['file'] == runtime.name and entry['symbols'] == debug.name
        assert entry['binarySha256'] == hashlib.sha256(runtime.read_bytes()).hexdigest()
        assert entry['symbolsSha256'] == hashlib.sha256(debug_bytes).hexdigest()
        for binary in (original, runtime, debug):
            assert 'Build ID: ' + entry['buildId'] in output('readelf', '-n', binary)
    assert archives[0] == archives[1], 'identical inputs must produce identical archives'
    assert hashlib.sha256(original.read_bytes()).hexdigest() == original_hash
    assert '.debug_info' in output('readelf', '-SW', original)
print('Release symbol packaging: hashes, build IDs, DWARF split, exports, behavior and reproducibility passed')
