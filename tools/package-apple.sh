#!/bin/bash
# MinkowskiKart - Package for macOS Distribution
# (C) 2026 MinkowskiKart Team

set -e

# Default values
PROJECT_ROOT=$(realpath "$(dirname "$0")/..")
BUILD_DIR="${PROJECT_ROOT}/build"
OUT_DIR="${PROJECT_ROOT}/dist"
APP_NAME="MinkowskiKart"
VERSION="1.0.0"
SIGNING_ID=""

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --build-dir <dir>    Path to CMake build directory (default: build)"
    echo "  --out-dir <dir>      Path to output packaged files (default: dist)"
    echo "  --version <ver>      Version string (default: 1.0.0)"
    echo "  --sign <id>          Code signing identity"
    echo "  --help               Show this help"
    exit 1
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --build-dir) BUILD_DIR="$(realpath "$2")"; shift ;;
        --out-dir) OUT_DIR="$(realpath "$2")"; shift ;;
        --version) VERSION="$2"; shift ;;
        --sign) SIGNING_ID="$2"; shift ;;
        --help) usage ;;
        *) echo "Unknown parameter: $1"; usage ;;
    esac
    shift
done

echo "================================================"
echo "  MinkowskiKart - Package for macOS"
echo "================================================"
echo "Project Root: ${PROJECT_ROOT}"
echo "Build Dir:    ${BUILD_DIR}"
echo "Output Dir:   ${OUT_DIR}"
echo "Version:      ${VERSION}"
echo "================================================"

# 1. Verify build exists
# CMake puts the bundle in bin/ or root of build dir depending on generator
APP_BUNDLE="${BUILD_DIR}/bin/${APP_NAME}.app"
if [ ! -d "${APP_BUNDLE}" ]; then
    APP_BUNDLE="${BUILD_DIR}/${APP_NAME}.app"
fi

if [ ! -d "${APP_BUNDLE}" ]; then
    echo "ERROR: ${APP_NAME}.app not found in build directory."
    echo "Please build the project with CMake first."
    exit 1
fi

# 2. Setup staging
STAGING_DIR="${OUT_DIR}/staging"
mkdir -p "${STAGING_DIR}"
rm -rf "${STAGING_DIR}/${APP_NAME}.app"

echo "Copying App Bundle to staging..."
cp -R "${APP_BUNDLE}" "${STAGING_DIR}/"

TARGET_APP="${STAGING_DIR}/${APP_NAME}.app"
RESOURCES_DIR="${TARGET_APP}/Contents/Resources"
DATA_DIR="${RESOURCES_DIR}/data"

# 3. Handle data symlink/directory
# CMake post-build might have created a symlink. For distribution, we need real files.
if [ -L "${DATA_DIR}" ]; then
    echo "Resolving data symlink to real directory..."
    rm "${DATA_DIR}"
fi
mkdir -p "${DATA_DIR}"

# 4. Inject Assets
echo "Injecting data files..."
cp -R "${PROJECT_ROOT}/data/." "${DATA_DIR}/"

if [ -d "${PROJECT_ROOT}/stk-assets" ]; then
    echo "Injecting stk-assets..."
    cp -R "${PROJECT_ROOT}/stk-assets/." "${DATA_DIR}/"
else
    echo "WARNING: stk-assets directory not found at root. App may be incomplete."
fi

mkdir -p "${DATA_DIR}/replay"

# 5. Update Info.plist version if needed
# /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${VERSION}" "${TARGET_APP}/Contents/Info.plist"

# 6. Code Signing (if identity provided)
if [ -n "${SIGNING_ID}" ]; then
    echo "Code signing with identity: ${SIGNING_ID}..."
    # Sign nested binaries/frameworks if any (STK is mostly static)
    # codesign --force --options runtime --deep --sign "${SIGNING_ID}" "${TARGET_APP}"
    codesign --force --sign "${SIGNING_ID}" "${TARGET_APP}"
fi

# 7. Create Disk Image (DMG)
DMG_NAME="${APP_NAME}-${VERSION}-mac.dmg"
DMG_PATH="${OUT_DIR}/${DMG_NAME}"

if command -v hdiutil > /dev/null; then
    echo "Creating DMG: ${DMG_NAME}..."
    rm -f "${DMG_PATH}"
    hdiutil create -volname "${APP_NAME}" -srcfolder "${STAGING_DIR}" -ov -format UDZO "${DMG_PATH}"
    echo "DMG created at: ${DMG_PATH}"
else
    echo "hdiutil not found, skipping DMG creation."
    echo "Creating ZIP archive instead..."
    ZIP_PATH="${OUT_DIR}/${APP_NAME}-${VERSION}-mac.zip"
    (cd "${STAGING_DIR}" && zip -r "${ZIP_PATH}" "${APP_NAME}.app")
    echo "ZIP created at: ${ZIP_PATH}"
fi

# 8. Cleanup staging
# rm -rf "${STAGING_DIR}"

echo "================================================"
echo "  PACKAGING SUCCESSFUL"
echo "================================================"
