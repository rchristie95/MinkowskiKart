#!/bin/bash
# MinkowskiKart - Build for Apple Platforms (macOS/iOS)
# (C) 2026 MinkowskiKart Team

set -e

PROJECT_ROOT=$(realpath "$(dirname "$0")/..")
BUILD_DIR="${PROJECT_ROOT}/build-apple"
TARGET_PLATFORM="macOS" # macOS or iOS
BUILD_TYPE="Release"
CLEAN_BUILD=false

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --platform <p>      Target platform: macOS or iOS (default: macOS)"
    echo "  --build-dir <dir>   Build directory (default: build-apple)"
    echo "  --type <type>       Build type: Release or Debug (default: Release)"
    echo "  --clean             Perform a clean build"
    echo "  --help              Show this help"
    exit 1
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --platform) TARGET_PLATFORM="$2"; shift ;;
        --build-dir) BUILD_DIR="$(realpath "$2")"; shift ;;
        --type) BUILD_TYPE="$2"; shift ;;
        --clean) CLEAN_BUILD=true ;;
        --help) usage ;;
        *) echo "Unknown parameter: $1"; usage ;;
    esac
    shift
done

echo "================================================"
echo "  MinkowskiKart - Build for ${TARGET_PLATFORM}"
echo "================================================"
echo "Project Root: ${PROJECT_ROOT}"
echo "Build Dir:    ${BUILD_DIR}"
echo "Build Type:   ${BUILD_TYPE}"
echo "================================================"

if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCHECK_ASSETS=OFF" # Assets injected during packaging
)

if [ "$TARGET_PLATFORM" == "iOS" ]; then
    echo "Configuring for iOS..."
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${PROJECT_ROOT}/cmake/Toolchain-ios-xcode.cmake"
        "-G" "Xcode"
    )
elif [ "$TARGET_PLATFORM" == "macOS" ]; then
    echo "Configuring for macOS..."
    # On macOS, we prefer Ninja if available, otherwise Unix Makefiles
    if command -v ninja > /dev/null; then
        CMAKE_ARGS+=("-G" "Ninja")
    fi
else
    echo "ERROR: Invalid platform '${TARGET_PLATFORM}'. Use 'macOS' or 'iOS'."
    exit 1
fi

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

echo "Starting build..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target supertuxkart --parallel $(sysctl -n hw.ncpu)

echo "================================================"
echo "  BUILD SUCCESSFUL"
echo "================================================"
