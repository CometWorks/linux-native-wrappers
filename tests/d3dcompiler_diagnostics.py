#!/usr/bin/env python3
"""Checks compile diagnostics coming out of the real d3dcompiler_47.dll.

The DLL formats its messages through the CRT printf shims in the loader, which
have to honour the width the format's length modifier implies. Reading a full
64-bit slot for a plain %d used to append stack garbage to every number, so a
diagnostic read '(6,1234567890123-32): error X1234567890456: ...' instead of
'(6,17-32): error X3004: ...'.

Usage: d3dcompiler_diagnostics.py <d3dcompiler_tool> <d3dcompiler_47.dll>
"""

import re
import subprocess
import sys
from pathlib import Path


def main():
    tool, dll = sys.argv[1], sys.argv[2]
    shaders = Path(__file__).parent / "shaders"
    bad = shaders / "Bad.hlsl"

    proc = subprocess.run(
        [tool, "compile", dll, "-", str(bad), str(shaders), "PS", "ps_5_0", "1"],
        capture_output=True,
        text=True,
    )
    output = proc.stdout + proc.stderr
    if proc.returncode == 0:
        print("FAIL: compiling Bad.hlsl succeeded")
        print(output)
        return 1

    # 'undeclared_thing' sits on line 6, columns 17-32.
    expected = re.compile(r"\(6,17-32\): error X3004: undeclared identifier 'undeclared_thing'")
    if not expected.search(output):
        print("FAIL: diagnostic does not carry the expected line, column and code")
        print("---- output ----")
        print(output)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
