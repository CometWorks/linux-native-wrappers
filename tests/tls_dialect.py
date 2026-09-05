"""Fail when a wrapper library imports __tls_get_addr.

The wrappers are built with -mtls-dialect=gnu2 because GCC miscompiles the
traditional __tls_get_addr sequence inside ms_abi functions. See the comment
next to add_compile_options in CMakeLists.txt.
"""
import subprocess
import sys


def imports_tls_get_addr(library: str) -> bool:
    symbols = subprocess.run(
        ["readelf", "--dyn-syms", "--wide", library],
        check=True, capture_output=True, text=True,
    ).stdout
    return "__tls_get_addr" in symbols


def main(libraries: list[str]) -> int:
    offenders = [library for library in libraries if imports_tls_get_addr(library)]
    for library in offenders:
        print(f"{library}: imports __tls_get_addr, build with -mtls-dialect=gnu2", file=sys.stderr)
    print(f"checked {len(libraries)} libraries, {len(offenders)} import __tls_get_addr")
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
