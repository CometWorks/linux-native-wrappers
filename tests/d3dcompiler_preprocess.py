#!/usr/bin/env python3
"""Checks D3DPreprocess through the wrapper against the real d3dcompiler_47.dll.

Preprocesses tests/shaders/Root.hlsl via d3dcompiler_tool and asserts the
properties the Space Engineers shader cache depends on:

- the root file is announced as '#line 1 ""' (empty source name),
- local includes resolve relative to the including file, case-insensitively,
- <system> includes resolve through the include directory,
- macros passed on the command line are expanded,
- #line directives reference include names as written in the source.

Usage: d3dcompiler_preprocess.py <d3dcompiler_tool> <d3dcompiler_47.dll>
"""

import subprocess
import sys
from pathlib import Path


def main():
    tool, dll = sys.argv[1], sys.argv[2]
    shaders = Path(__file__).parent / "shaders"
    root = shaders / "Root.hlsl"

    proc = subprocess.run(
        [tool, "preprocess", dll, "-", str(root), str(shaders), "EXTRA_VALUE=7"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print(proc.stderr)
        print("FAIL: preprocess exited nonzero")
        return 1

    text = proc.stdout
    checks = [
        ('#line 1 ""', "root file uses an empty source name"),
        ('#line 1 "Sub/Local.hlsli"', "local include name kept as written"),
        ('#line 1 "nested.HLSLI"', "case-insensitive nested include resolved"),
        ('#line 1 "SystemInc.hlsli"', "system include resolved via include dir"),
        ("float4 ( 0.25 , 0 , 0 , 0 )", "LOCAL_VALUE macro expanded"),
        ("float4 ( 0 , 0.25 , 0 , 0 )", "SYSTEM_VALUE macro expanded"),
        ("float4 ( 0 , 0 , 0.25 , 0 )", "NESTED_VALUE macro expanded"),
        ("+ 7", "command-line macro EXTRA_VALUE expanded"),
    ]
    failed = False
    for needle, description in checks:
        if needle not in text:
            print(f"FAIL: {description}: missing {needle!r}")
            failed = True
    if "#include" in text:
        print("FAIL: unexpanded #include directive in output")
        failed = True

    if failed:
        print("---- output ----")
        print(text)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
