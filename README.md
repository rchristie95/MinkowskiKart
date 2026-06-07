# Minkowski Kart: Relativistic Racing

Minkowski Kart is a 3D arcade kart-racing game built around the principles of special and general relativity. Originally forked from [SuperTuxKart](https://github.com/supertuxkart/stk-code), it transforms the racing experience by integrating relativistic effects into every aspect of gameplay, from the item system to the visual rendering.

Owned online multiplayer deployment and invitation setup are documented in
[doc/ONLINE_INFRASTRUCTURE.md](doc/ONLINE_INFRASTRUCTURE.md).

## Relativistic Item System

The core of Minkowski Kart is its spacetime-themed powerup system. Each item is designed to reflect relativistic concepts:

| Powerup | Effect |
|---|---|
| **Warp Bubble** | A defensive shield that provides a short max-speed boost when fired forward. |
| **Asteroid** | A heavy, rocky projectile that flies in a ballistic arc. |
| **Black Hole** | A heavy homing projectile that orbits and pulls in targets. |
| **Zipper** | A standard high-energy speed boost. |
| **Photon** | Launches a high-speed tether on impact or triggers a Doppler-style blast. |
| **Super Position** | A global spacetime collapse that rotates track pickups and pulses the world. |
| **Anti-Karticle** | A mirrored clone that annihilates on contact with a pair-production flash. |
| **Wormhole** | Spawns a linked pair of traversable spacetime portals. |
| **Time Dilation** | A field-effect that slows down all other active karts. |
| **Maxwell-Boltzmann** | Targets the leader with deterministic Gaussian velocity kicks. |

The gameplay logic lives in `src/items/` and item weights are configured in [data/powerup.xml](data/powerup.xml).

## Visual Effects and Rendering

Minkowski Kart features specialized visual effects to represent relativistic phenomena:

- **Doppler Shifting:** Fullscreen flashes and color shifts during high-speed impacts.
- **Spacetime Collapses:** Visual pulses and world-distortion during Super Position events.
- **Gravitational Visuals:** Specialized rendering for Black Hole and Wormhole entities.
- **Relativistic Apparent Position:** Calculations for world-space velocity effects.

### Track Clipping Mitigation

The Relativity options menu provides two local rendering modes for reducing kart/track clipping:

- **Cheap (lite subdivision + height correction):** Uses GPU subdivision and adjusts visual height against the physical track surface.
- **Enhanced (strong subdivision):** Uses advanced tessellation shaders for solid and normal-mapped meshes with a stronger near-kart cutoff. Edge subdivision is dynamically scaled based on distance.

Enhanced mode requires desktop GLSL 4.00+ or GLSL ES 3.20+.

## Building The Project (Windows)

This project uses CMake and Ninja with the llvm-mingw toolchain.

### 1. Prerequisites

- **CMake:** `winget install --id Kitware.CMake -e`
- **llvm-mingw:** Download from [llvm-mingw releases](https://github.com/mstorsjo/llvm-mingw/releases) and extract to `.build-tools\llvm-mingw\`.
- **Ninja:** Download from [Ninja releases](https://github.com/ninja-build/ninja/releases) and extract to `.build-tools\ninja\`.
- **Dependencies:** Download Windows dependencies from the project repository and extract to the root directory.

### 2. Configure and Build

```bash
# Configure
cmake -S . -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=".build-tools/ninja/ninja.exe" \
  -DLLVM_ARCH=x86_64 \
  -DLLVM_PREFIX=".build-tools/llvm-mingw/llvm-mingw-..." \
  -DCMAKE_TOOLCHAIN_FILE="cmake/Toolchain-llvm-mingw.cmake" \
  -DCHECK_ASSETS=OFF \
  -DUSE_WIIUSE=OFF

# Build
.build-tools/ninja/ninja.exe -C build -j4
```

The executable is output to `build\bin\MinkowskiKart.exe`.

### 3. Run

```bat
run.bat
```

## Credits and Attribution

### Project Foundation
This project was forked from SuperTuxKart (GPLv3). We are grateful to the SuperTuxKart team for their robust racing engine.

### Relativity Concepts
Relativistic rendering and math ideas are adapted from **OpenRelativity** by the MIT Game Lab (MIT License).

---
Developed as an experimental exploration of relativistic physics in arcade gaming.
