# Minkowski Kart: Relativistic Racing

Minkowski Kart is a specialized fork of **SuperTuxKart**, integrated with **OpenRelativity** to simulate the visual and physical effects of special and general relativity at high velocities. This project transforms the standard kart racing experience into a playground for spacetime physics.

## 🚀 Overview

This version of the game introduces a suite of "Relativistic Power-ups," replacing traditional items with tools that manipulate the local metric of spacetime. From frame-dragging accretion disks to Lorentz grid transformations, every visual effect is designed to communicate fundamental relativistic concepts.

## ✨ New Relativistic VFX

We have implemented nine unique visual effects using custom GLSL shaders and C++ logic:

1.  **Warp Bubble (Shield):** A transparent spherical shell with an **Einstein ring** rim. It features background refraction and concentric ripple effects upon blocking hits.
2.  **Kerr Black Hole (Bowling Ball):** Replaces the bowling ball with a rotating black hole featuring a glowing **spiral accretion disk**, relativistic beaming, and a blue-shifted ergosphere.
3.  **Cosmic String (Plunger/Grapple):** 
    *   **Forward:** An infinitely thin, flickering filament that acts as a surgical grapple.
    *   **Backward:** A "Blackboard Gag" that slams an academic chalkboard onto the victim's view, covered in erased chalk dust and **Einstein’s field equations**.
4.  **Geodesic Missile (Rubber Ball):** A compact white-gold core that follows the easiest route through curved spacetime, leaving a bent light-path trail.
5.  **Time-Dilation Field (Parachute):** Shoves the victim into a slow patch of time, visualized by a **redshift-biased halo** and smeared motion trails.
6.  **Mass Spike (Anvil):** Simulates catastrophic local density by visually **compressing the kart’s suspension** and surrounding it with a dense downward shimmer.
7.  **Frame Shift (Switch):** A sweeping **Lorentz grid wave** that reinterprets pickups and hazards as it passes over them.
8.  **Neutron Star (Cake):** A brutally compact, extremely bright projectile representing maximum matter density.
9.  **Tidal Arm (Swatter):** A short-range distortion arc that "spaghettifies" space to smack away nearby rivals.

## 🛠️ Building the Project

This project uses **CMake** and **Ninja** for compilation.

### Prerequisites
*   LLVM-MinGW toolchain (bundled in `.build-tools/llvm-mingw`)
*   Ninja (bundled in `.build-tools/ninja`)

### Compilation
1.  Ensure your `build/` directory contains a valid `build.ninja` and `CMakeCache.txt`.
2.  Run the following command from the root directory:
    ```powershell
    .build-tools\ninja\ninja.exe -C build
    ```
3.  The binary will be generated at: `build\bin\supertuxkart.exe`

## 📜 Citations and Credits

### SuperTuxKart
This project is built upon the **SuperTuxKart** engine. We are deeply grateful to the SuperTuxKart team for their decades of work on this premier open-source racing game.
*   **Official Website:** [supertuxkart.net](https://supertuxkart.net)
*   **License:** GNU General Public License v3 (GPLv3)

### OpenRelativity
The relativistic physics and rendering logic are powered by **OpenRelativity**, originally developed by the **MIT Game Lab**. 
*   **Original Toolkit:** [OpenRelativity on GitHub](https://github.com/MITGameLab/OpenRelativity)
*   **License:** MIT License
*   *Note:* This project adapts OpenRelativity's shaders and math for the SuperTuxKart rendering pipeline.

---
*Developed as an educational and experimental mod for MinkowskiKart.*
