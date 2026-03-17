#!/bin/bash
# StrayLight OS ISO build script
# Requires: live-build, debootstrap, root privileges
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
VERSION="${STRAYLIGHT_VERSION:-1.0.0}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT_NAME="straylight-os-${VERSION}-amd64"

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --clean         Clean previous build artifacts"
    echo "  --config-only   Run lb config only (for debugging)"
    echo "  --no-cache      Disable package cache"
    echo "  --repo URL      Custom APT repository for StrayLight packages"
    echo "  --help          Show this help"
    exit 0
}

log() { echo "[$(date -u +%H:%M:%S)] $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# Parse arguments
CLEAN=false
CONFIG_ONLY=false
USE_CACHE=true
CUSTOM_REPO=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)       CLEAN=true; shift ;;
        --config-only) CONFIG_ONLY=true; shift ;;
        --no-cache)    USE_CACHE=false; shift ;;
        --repo)        CUSTOM_REPO="$2"; shift 2 ;;
        --help)        usage ;;
        *)             die "Unknown option: $1" ;;
    esac
done

# Preflight checks
[[ $EUID -eq 0 ]] || die "Must run as root (use sudo)"
command -v lb >/dev/null || die "live-build not installed: apt install live-build"
command -v debootstrap >/dev/null || die "debootstrap not installed: apt install debootstrap"

# Setup build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if $CLEAN; then
    log "Cleaning previous build..."
    lb clean --purge 2>/dev/null || true
    rm -rf .build/ cache/ chroot/ binary/ binary.* "${OUTPUT_NAME}"*
    log "Clean complete."
    exit 0
fi

# Copy configuration
log "Setting up live-build configuration..."
mkdir -p auto config

cp "${SCRIPT_DIR}/auto/config" auto/
cp "${SCRIPT_DIR}/auto/build"  auto/
cp "${SCRIPT_DIR}/auto/clean"  auto/
chmod +x auto/*

cp -a "${SCRIPT_DIR}/config/"* config/ 2>/dev/null || true

# Add custom repository if specified
if [[ -n "${CUSTOM_REPO}" ]]; then
    log "Adding custom repository: ${CUSTOM_REPO}"
    mkdir -p config/archives
    echo "deb ${CUSTOM_REPO} bookworm main" > config/archives/straylight.list.chroot
    # If a GPG key is needed, add it:
    # cp /path/to/straylight-archive-keyring.gpg config/archives/straylight.key.chroot
fi

# Disable cache if requested
if ! $USE_CACHE; then
    export LB_CACHE=false
    export LB_CACHE_PACKAGES=false
fi

# Run lb config
log "Running lb config..."
lb config

if $CONFIG_ONLY; then
    log "Config-only mode: stopping here."
    exit 0
fi

# Build the ISO
log "Building StrayLight OS ISO... (this will take 15-45 minutes)"
lb build 2>&1 | tee "${SCRIPT_DIR}/build-${TIMESTAMP}.log"

# Move output
if [[ -f live-image-amd64.hybrid.iso ]]; then
    mv live-image-amd64.hybrid.iso "${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "ISO created: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "Size: $(du -h "${SCRIPT_DIR}/${OUTPUT_NAME}.iso" | cut -f1)"

    # Generate checksums
    cd "${SCRIPT_DIR}"
    sha256sum "${OUTPUT_NAME}.iso" > "${OUTPUT_NAME}.iso.sha256"
    log "Checksum: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso.sha256"
elif [[ -f "${OUTPUT_NAME}.hybrid.iso" ]]; then
    mv "${OUTPUT_NAME}.hybrid.iso" "${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "ISO created: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
else
    die "Build completed but no ISO file found. Check build log."
fi

log "Build complete!"
