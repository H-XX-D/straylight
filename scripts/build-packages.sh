#!/usr/bin/env bash
# build-packages.sh — Build all StrayLight OS .deb packages
# Usage: ./scripts/build-packages.sh [options] [package...]
#
# Requires: dpkg-buildpackage, debhelper (>= 13), dh-cmake, cmake (>= 3.25)
# Must run on Debian Bookworm/Trixie amd64.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PACKAGING_DIR="$ROOT_DIR/packaging"
OUTPUT_DIR="$ROOT_DIR/output/debs"
VERSION="${STRAYLIGHT_VERSION:-1.0.0}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"

# Package build order (respects dependencies)
PACKAGES=(
    straylight-common       # libs — no deps
    straylight-kernel       # DKMS modules — depends on linux-headers
    straylight-core         # core daemons — depends on common
    straylight-compositor   # Wayland compositor + shell — depends on common
    straylight-ml           # ML subsystems — depends on common + core
    straylight-network      # network subsystems — depends on common + core
    straylight-exotic       # exotic subsystems — depends on common + core
    straylight-desktop      # desktop apps — depends on common + core + compositor
    straylight-os           # metapackage — depends on all above
)

CLEAN=0
NO_SIGN=0
CHECK_DEPS=0
PARALLEL=$(nproc 2>/dev/null || echo 4)
SELECTED_PACKAGES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)       CLEAN=1; shift ;;
        --no-sign)     NO_SIGN=1; shift ;;
        --check-deps)  CHECK_DEPS=1; shift ;;
        --parallel)    PARALLEL="$2"; shift 2 ;;
        --output-dir)  OUTPUT_DIR="$2"; shift 2 ;;
        straylight-*)  SELECTED_PACKAGES+=("$1"); shift ;;
        -h|--help)
            echo "Usage: $0 [options] [package...]"
            echo ""
            echo "Options:"
            echo "  --clean           Remove previous build artifacts before building"
            echo "  --no-sign         Skip GPG signing (-us -uc)"
            echo "  --check-deps      Only check build-dependencies, do not build"
            echo "  --parallel N      Number of parallel make jobs (default: nproc)"
            echo "  --output-dir DIR  Output directory for .deb files"
            echo ""
            echo "If no packages are specified, all packages are built in dependency order."
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Use selected packages or default build order
if [[ ${#SELECTED_PACKAGES[@]} -gt 0 ]]; then
    BUILD_PACKAGES=("${SELECTED_PACKAGES[@]}")
else
    BUILD_PACKAGES=("${PACKAGES[@]}")
fi

mkdir -p "$OUTPUT_DIR"
BUILD_LOG="$OUTPUT_DIR/build-${TIMESTAMP}.log"

echo "======================================================="
echo "  StrayLight OS Package Builder v${VERSION}"
echo "======================================================="
echo ""
echo "Source root:   $ROOT_DIR"
echo "Output:        $OUTPUT_DIR"
echo "Parallel jobs: $PARALLEL"
echo "Build log:     $BUILD_LOG"
echo ""

# Verify we're on Debian/Ubuntu
if ! command -v dpkg-buildpackage &>/dev/null; then
    echo "ERROR: dpkg-buildpackage not found. Install: apt install dpkg-dev debhelper"
    exit 1
fi

# Check build dependencies only
if [[ $CHECK_DEPS -eq 1 ]]; then
    DEPS_FAILED=0
    for pkg in "${BUILD_PACKAGES[@]}"; do
        PKG_DIR="$PACKAGING_DIR/$pkg"
        [[ -d "$PKG_DIR/debian" ]] || continue
        echo "Checking deps for $pkg..."
        cd "$PKG_DIR"
        dpkg-checkbuilddeps 2>&1 || DEPS_FAILED=$((DEPS_FAILED + 1))
    done
    if [[ $DEPS_FAILED -gt 0 ]]; then
        echo "ERROR: $DEPS_FAILED package(s) have unmet build dependencies."
        exit 1
    fi
    echo "All build dependencies satisfied."
    exit 0
fi

# Build options for dpkg-buildpackage
DPKG_OPTS=("-j${PARALLEL}" "--build=binary")
if [[ $NO_SIGN -eq 1 ]]; then
    DPKG_OPTS+=("-us" "-uc")
fi

BUILT=0
FAILED=0
FAILED_PKGS=()

for pkg in "${BUILD_PACKAGES[@]}"; do
    PKG_DIR="$PACKAGING_DIR/$pkg"

    if [[ ! -d "$PKG_DIR/debian" ]]; then
        echo "SKIP: $pkg — no debian/ directory at $PKG_DIR/debian"
        continue
    fi

    echo "-------------------------------------------------------"
    echo "Building: $pkg"
    echo "-------------------------------------------------------"

    # Create a temporary build directory with source + debian/
    BUILD_TMP=$(mktemp -d "/tmp/straylight-build-${pkg}-XXXXXX")
    trap "rm -rf $BUILD_TMP" EXIT

    # Copy source tree (excluding .git and output)
    rsync -a --exclude='.git' --exclude='output/' --exclude='build/' \
        "$ROOT_DIR/" "$BUILD_TMP/src/"

    # Overlay the package-specific debian/ directory
    cp -a "$PKG_DIR/debian" "$BUILD_TMP/src/debian"

    pushd "$BUILD_TMP/src" >/dev/null

    if [[ $CLEAN -eq 1 ]]; then
        debian/rules clean 2>/dev/null || true
    fi

    # Build the package
    if DEB_BUILD_OPTIONS="parallel=$PARALLEL" \
       dpkg-buildpackage "${DPKG_OPTS[@]}" 2>&1 | tee -a "$BUILD_LOG"; then
        # Move .deb files to output directory
        find "$BUILD_TMP" -maxdepth 1 -name "*.deb" -exec mv {} "$OUTPUT_DIR/" \; 2>/dev/null || true
        find "$BUILD_TMP" -maxdepth 1 -name "*.ddeb" -exec mv {} "$OUTPUT_DIR/" \; 2>/dev/null || true
        find "$BUILD_TMP" -maxdepth 1 -name "*.changes" -exec mv {} "$OUTPUT_DIR/" \; 2>/dev/null || true
        find "$BUILD_TMP" -maxdepth 1 -name "*.buildinfo" -exec mv {} "$OUTPUT_DIR/" \; 2>/dev/null || true
        echo "OK: $pkg — built successfully"
        BUILT=$((BUILT + 1))
    else
        echo "FAIL: $pkg — BUILD FAILED (see $BUILD_LOG)"
        FAILED=$((FAILED + 1))
        FAILED_PKGS+=("$pkg")
    fi

    popd >/dev/null
    rm -rf "$BUILD_TMP"
    trap - EXIT
done

echo ""
echo "======================================================="
echo "Build complete: $BUILT succeeded, $FAILED failed"
echo "Packages in:   $OUTPUT_DIR"
echo "Build log:     $BUILD_LOG"

if [[ $FAILED -gt 0 ]]; then
    echo "Failed packages: ${FAILED_PKGS[*]}"
    exit 1
fi

# Generate Packages index for local apt repo
if command -v dpkg-scanpackages &>/dev/null && [[ $BUILT -gt 0 ]]; then
    echo ""
    echo "Generating local APT repository index..."
    cd "$OUTPUT_DIR"
    dpkg-scanpackages . /dev/null > Packages 2>/dev/null
    gzip -k -f Packages
    echo "Done. Add to sources.list:"
    echo "  deb [trusted=yes] file://$OUTPUT_DIR ./"
fi

echo "All packages built successfully."
