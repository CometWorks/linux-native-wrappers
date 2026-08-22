#!/usr/bin/env bash
#
# Clean-builds every wrapper in both configurations and packages the shared
# libraries into the per-game release archives under dist/:
#
#   dist/se1-native-wrappers.tar.gz         SE1, Release (-O3 -DNDEBUG)
#   dist/se2-native-wrappers.tar.gz         SE2, Release (-O3 -DNDEBUG)
#   dist/se1-native-wrappers.debug.tar.gz   SE1, Debug   (-O0 -g)
#   dist/se2-native-wrappers.debug.tar.gz   SE2, Debug   (-O0 -g)
#
# Each archive holds only the libraries its game version loads, at the archive
# root, with no subdirectory. The build workflow runs this script, so a locally
# produced archive matches the published one.
#
# Release is built and packaged first, Debug second. Each configuration starts
# from `make clean`, so build/ is left holding the Debug libraries: local
# development gets unoptimized binaries with full symbols and usable
# backtraces without another build.
#
# Usage:
#
#   ./build_and_package.sh
#
# Requires x86-64 Linux, CMake 3.13 or newer, Python 3, Make, and a
# C++17-capable g++.

set -euo pipefail

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_DIR/build"
DIST_DIR="$REPO_DIR/dist"

# Wrappers each game version loads. Keep in sync with the "Libraries built"
# table in README.md.
SE1_LIBS=(
    libD3DCompiler.so
    libHavok.so
    libRecastDetour.so
    libVRageNative.so
)
SE2_LIBS=(
    libVRage.KytheraV2.Native.so
    libVRage.Physics.Native.so
    libVRage.Platform.Windows.Native.so
    libVRage.Slug.Native.so
    libVRage.Voxels.Native.so
)

# Collapsible log sections when running under GitHub Actions, plain headings
# everywhere else.
group() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::group::$1"
    else
        echo
        echo "==> $1"
    fi
}

endgroup() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::endgroup::"
    fi
}

package_set() {
    # package_set <staging dir> <archive> <lib>...
    local staging="$1" archive="$2"
    shift 2

    echo
    echo "==> Packaging $archive"

    rm -rf "$staging"
    mkdir -p "$staging"
    local lib
    for lib in "$@"; do
        if [ ! -f "$BUILD_DIR/$lib" ]; then
            echo "ERROR: expected build output $BUILD_DIR/$lib is missing." >&2
            exit 1
        fi
        install -m 0755 "$BUILD_DIR/$lib" "$staging/$lib"
    done

    # --sort=name plus a fixed mtime/owner make the archive byte-reproducible
    # for a given set of inputs, so an unchanged rebuild produces an identical
    # file.
    rm -f "$DIST_DIR/$archive"
    tar --sort=name \
        --owner=0 --group=0 --numeric-owner \
        --mtime='UTC 2020-01-01' \
        -czf "$DIST_DIR/$archive" -C "$staging" "$@"
    rm -rf "$staging"

    tar -tzf "$DIST_DIR/$archive" | sed 's/^/    /'
}

build_config() {
    # build_config <build type> <archive suffix>
    local build_type="$1" suffix="$2"

    group "Build $build_type"
    # A clean tree per configuration: no objects survive the build type change,
    # and the last configuration built is the one left in build/.
    make -C "$REPO_DIR" clean
    cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$build_type"
    cmake --build "$BUILD_DIR" -j "$(nproc)"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    endgroup

    package_set "$DIST_DIR/staging-se1" "se1-native-wrappers$suffix.tar.gz" "${SE1_LIBS[@]}"
    package_set "$DIST_DIR/staging-se2" "se2-native-wrappers$suffix.tar.gz" "${SE2_LIBS[@]}"
}

mkdir -p "$DIST_DIR"

# Release first, Debug last, so build/ ends up holding the Debug libraries.
build_config Release ""
build_config Debug   ".debug"

echo
echo "==> Done. build/ holds the Debug libraries."
ls -lh "$DIST_DIR"/*.tar.gz | sed 's/^/    /'
