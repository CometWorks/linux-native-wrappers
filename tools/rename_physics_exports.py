import sys
from pathlib import Path

from elf_exports import rewrite_exports


if len(sys.argv) != 3:
    raise SystemExit(f"usage: {sys.argv[0]} ELF_FILE EXPORT_MANIFEST")

path, manifest_path = sys.argv[1:]
renames = dict(line.rstrip("\n").split("\t", 1)
               for line in Path(manifest_path).read_text(encoding="utf-8").splitlines()
               if line.strip())
if not renames or any(len(old) != len(new) for old, new in renames.items()):
    raise SystemExit("expected equal-length Physics export mappings")

renames = {old.encode(): new.encode() for old, new in renames.items()}
renamed = rewrite_exports(path, renames.get)
if renamed != len(renames):
    raise SystemExit(f"expected {len(renames)} Physics exports, found {renamed}")
