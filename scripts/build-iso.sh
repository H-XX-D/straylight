#!/usr/bin/env bash
# build-iso.sh — Build StrayLight OS live ISO image via live-build
# Usage: ./scripts/build-iso.sh [options]
#
# Requires: live-build (>= 1:20230502), debootstrap, squashfs-tools,
#           xorriso, grub-pc-bin, grub-efi-amd64-bin
# Must run as root on Debian Bookworm/Trixie amd64.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO_DIR="$ROOT_DIR/iso/live-build"
OUTPUT_DIR="$ROOT_DIR/output"
VERSION="${STRAYLIGHT_VERSION:-1.0.0}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
ARCH="amd64"
CLEAN=0
CONFIG_ONLY=0
NO_CACHE=0
CUSTOM_REPO=""

log()  { echo "[$(date -u +%H:%M:%S)] $*"; }
die()  { echo "ERROR: $*" >&2; exit 1; }
warn() { echo "WARN:  $*" >&2; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)       CLEAN=1; shift ;;
        --config-only) CONFIG_ONLY=1; shift ;;
        --no-cache)    NO_CACHE=1; shift ;;
        --repo)        CUSTOM_REPO="$2"; shift 2 ;;
        --arch)        ARCH="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --clean        Remove previous build before starting"
            echo "  --config-only  Run lb config only (for debugging)"
            echo "  --no-cache     Disable package cache"
            echo "  --repo URL     Custom APT repository for StrayLight packages"
            echo "  --arch ARCH    Target architecture (default: amd64)"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

echo "======================================================="
echo "  StrayLight OS ISO Builder v${VERSION}"
echo "======================================================="
echo ""
echo "Project root: $ROOT_DIR"
echo "ISO dir:      $ISO_DIR"
echo "Output dir:   $OUTPUT_DIR"
echo "Architecture: $ARCH"
echo ""

# Preflight checks
[[ $EUID -eq 0 ]] || die "ISO build requires root. Run with: sudo $0"
command -v lb >/dev/null || die "live-build not found: apt install live-build"
command -v debootstrap >/dev/null || die "debootstrap not found: apt install debootstrap"
command -v xorriso >/dev/null || warn "xorriso not found: apt install xorriso"

mkdir -p "$OUTPUT_DIR"
BUILD_LOG="$OUTPUT_DIR/iso-build-${TIMESTAMP}.log"

# Change into the live-build working directory
cd "$ISO_DIR"

if [[ $CLEAN -eq 1 ]]; then
    log "Cleaning previous build..."
    lb clean --all 2>/dev/null || true
    rm -f "$OUTPUT_DIR"/straylight-os-*.iso "$OUTPUT_DIR"/straylight-os-*.sha256
    log "Clean complete."
    if [[ $CONFIG_ONLY -eq 0 ]]; then
        # After clean, exit if only --clean was requested
        :
    fi
fi

# Export version for auto/config
export STRAYLIGHT_VERSION="$VERSION"

# Disable cache if requested
if [[ $NO_CACHE -eq 1 ]]; then
    export LB_CACHE=false
    export LB_CACHE_PACKAGES=false
fi

# Run lb config via auto/config
log "Configuring live-build..."
bash auto/config

# Add custom repository if specified
if [[ -n "${CUSTOM_REPO}" ]]; then
    log "Adding custom repository: ${CUSTOM_REPO}"
    mkdir -p config/archives
    echo "deb ${CUSTOM_REPO} bookworm main" > config/archives/straylight.list.chroot
fi

# Inject local .deb packages if available
DEB_DIR="$OUTPUT_DIR/debs"
if [[ -d "$DEB_DIR" ]] && ls "$DEB_DIR"/*.deb &>/dev/null; then
    log "Injecting local .deb packages from $DEB_DIR..."
    LOCAL_PKG_DIR="config/packages.chroot"
    mkdir -p "$LOCAL_PKG_DIR"
    cp "$DEB_DIR"/*.deb "$LOCAL_PKG_DIR/"
    INJECTED=$(ls "$LOCAL_PKG_DIR"/*.deb | wc -l)
    log "Injected ${INJECTED} package(s) into chroot"
else
    warn "No local .deb packages found in $DEB_DIR"
    warn "Run ./scripts/build-packages.sh first, or packages will be fetched from apt."
fi

# Install Calamares configuration into the ISO skeleton
log "Installing Calamares configuration..."
CALAMARES_SRC="$ROOT_DIR/iso/calamares"
CALAMARES_DST="config/includes.chroot/etc/calamares"
mkdir -p "$CALAMARES_DST/modules" "$CALAMARES_DST/branding/straylight"

cp "$CALAMARES_SRC/settings.conf" "$CALAMARES_DST/"
cp "$CALAMARES_SRC/modules/"*.conf "$CALAMARES_DST/modules/" 2>/dev/null || true

if [[ -d "$CALAMARES_SRC/branding/straylight" ]]; then
    cp -a "$CALAMARES_SRC/branding/straylight/"* "$CALAMARES_DST/branding/straylight/"
fi

# Install D-Bus policy files into chroot skeleton
log "Installing D-Bus policy files..."
DBUS_DST="config/includes.chroot/etc/dbus-1/system.d"
mkdir -p "$DBUS_DST"
cp "$ROOT_DIR/services/dbus/"*.conf "$DBUS_DST/" 2>/dev/null || true

# Install udev rules into chroot skeleton
log "Installing udev rules..."
UDEV_DST="config/includes.chroot/etc/udev/rules.d"
mkdir -p "$UDEV_DST"
cp "$ROOT_DIR/services/udev/"*.rules "$UDEV_DST/" 2>/dev/null || true

# Install sysctl tweaks into chroot skeleton
log "Installing sysctl configuration..."
SYSCTL_DST="config/includes.chroot/etc/sysctl.d"
mkdir -p "$SYSCTL_DST"
cp "$ROOT_DIR/iso/live-build/config/includes.chroot/etc/sysctl.d/99-straylight.conf" \
    "$SYSCTL_DST/" 2>/dev/null || true

# Copy theme assets
log "Installing theme files..."
THEMES_DST="config/includes.chroot/usr/share/straylight/themes"
mkdir -p "$THEMES_DST"
if [[ -d "$ROOT_DIR/etc/straylight/themes" ]]; then
    find "$ROOT_DIR/etc/straylight/themes" -name "*.json" \
        -exec cp {} "$THEMES_DST/" \; 2>/dev/null || true
fi

# Copy icon assets
ICONS_DST="config/includes.chroot/usr/share/straylight/icons"
mkdir -p "$ICONS_DST"
if [[ -d "$ROOT_DIR/assets/icons" ]]; then
    cp -a "$ROOT_DIR/assets/icons/"* "$ICONS_DST/" 2>/dev/null || true
fi

if [[ $CONFIG_ONLY -eq 1 ]]; then
    log "Config-only mode: stopping here. Run 'sudo lb build' in $ISO_DIR to continue."
    exit 0
fi

# Build the ISO
log "Building StrayLight OS ISO v${VERSION}... (this will take 15-45 minutes)"
log "Build log: $BUILD_LOG"
echo ""

lb build 2>&1 | tee "$BUILD_LOG"

# Move the ISO to output directory
ISO_FILE=$(ls "$ISO_DIR"/straylight-os-*.hybrid.iso \
              "$ISO_DIR"/live-image-${ARCH}.hybrid.iso \
              "$ISO_DIR"/straylight-os-${VERSION}-${ARCH}.iso \
              2>/dev/null | head -1 || true)

if [[ -n "$ISO_FILE" ]]; then
    FINAL_ISO="$OUTPUT_DIR/straylight-os-${VERSION}-${ARCH}.iso"
    mv "$ISO_FILE" "$FINAL_ISO"
    sha256sum "$FINAL_ISO" > "${FINAL_ISO}.sha256"

    echo ""
    echo "======================================================="
    log "ISO built successfully!"
    echo "  File:   $FINAL_ISO"
    echo "  Size:   $(du -h "$FINAL_ISO" | cut -f1)"
    echo "  SHA256: $(cut -d' ' -f1 "${FINAL_ISO}.sha256")"
    echo ""
    echo "  Test with QEMU:"
    echo "  qemu-system-x86_64 -cdrom $FINAL_ISO -m 8G -enable-kvm -smp 4"
    echo "======================================================="
else
    die "ISO file not found after build. Check log: $BUILD_LOG"
fi
