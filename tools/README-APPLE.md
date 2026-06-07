# MinkowskiKart Apple Build Suite

This directory contains scripts for building, packaging, and testing MinkowskiKart on Apple platforms (macOS and iOS).

## Scripts

### 1. `build-apple.sh`
Compiles the project using CMake.
- **Usage:** `./tools/build-apple.sh --platform [macOS|iOS] --type [Release|Debug]`
- **Requirements:** CMake, Ninja (optional but recommended), and Xcode Command Line Tools.

### 2. `package-apple.sh`
Packages the compiled executable into a standalone App Bundle (`.app`) and creates a distribution archive (`.dmg` or `.zip`).
- **Usage:** `./tools/package-apple.sh --build-dir <dir> --version <ver>`
- **Features:** Injects game data/assets, handles symlink resolution, and supports code signing.

### 3. `smoke-test-mac.sh`
Verifies the integrity of a macOS App Bundle.
- **Usage:** `./tools/smoke-test-mac.sh <path_to_app>`
- **Actions:** Launches the game, starts a race with scientist karts, and monitors for crashes during the first 10 seconds.

## Standard Workflow

```bash
# 1. Build
./tools/build-apple.sh --platform macOS

# 2. Package
./tools/package-apple.sh --build-dir build-apple --version 1.0.0

# 3. Test
./tools/smoke-test-mac.sh dist/staging/MinkowskiKart.app
```

These scripts are used by both local developers and the GitHub Actions CI pipeline to ensure consistency.
