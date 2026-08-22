import sys

from elf_exports import rewrite_exports


if len(sys.argv) != 2:
    raise SystemExit(f"usage: {sys.argv[0]} ELF_FILE")

path = sys.argv[1]
renamed = rewrite_exports(
    path, lambda name: name.replace(b"$", b"@") if b"$Slug$Terathon$$" in name else None)
if renamed != 13:
    raise SystemExit(f"expected 13 Slug exports, found {renamed}")
