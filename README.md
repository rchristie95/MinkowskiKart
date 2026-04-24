# Minkowski Kart: Relativistic Racing

Minkowski Kart is a specialized fork of SuperTuxKart integrated with
OpenRelativity to simulate visual and gameplay ideas drawn from special and
general relativity. It keeps the arcade kart-racing core, but remaps the item
system around spacetime-themed powerups, screen effects, and debuffs.

## Overview

This fork replaces most stock STK items with relativistic counterparts. The
current item set is defined in [data/powerup.xml](data/powerup.xml) and the
gameplay logic lives primarily in `src/items/`.

## Current Powerups

The current collectible powerups are:

1. `Warp Bubble`
   A defensive shield. Looking forward uses it as a protective bubble; looking
   backward still drops a trap bubblegum item. The active shield also grants a
   short max-speed boost.
2. `Asteroid`
   A fast, dense projectile that replaces the old cake slot.
3. `Black Hole`
   A slower, heavier homing projectile built on the bowling slot.
4. `Zipper`
   The standard speed boost, unchanged from STK.
5. `Photon`
   A reworked plunger slot. Forward fire creates a tether hit, while backward
   fire applies the same Doppler-style hit effect without the old central
   viewhole overlay.
6. `Super Position`
   A global item-collapse effect that still switches track pickups, now paired
   with relativistic VFX.
7. `Anti-Karticle`
   A mirrored anti-kart projectile replacing the old swatter slot.
8. `Wormhole`
   A linked pair of traversable portals replacing the old rubber-ball slot.
9. `Time Dilation`
   A field effect that applies slowdown attachments to other active karts.
   Racers ahead of the user still receive the strongest rank-scaled effect.
10. `Maxwell-Boltzmann`
    A leader-punish replacing the old harmonic/anvil slot. It targets the kart
    in first place for 10 seconds and applies deterministic Brownian velocity
    kicks in the track-tangent plane every second.

## Current Debuffs And Attachments

The current race debuffs and on-kart attachments are:

1. `Time Dilation`
   The main slowdown debuff. It is applied by the Time Dilation powerup and by
   direct banana hits. The old parachute mesh is hidden, but the slowdown and
   VFX remain active.
2. `Maxwell-Boltzmann`
   Applied to the current leader by the Maxwell-Boltzmann powerup. It lasts
   10 seconds, starts kicking after 1 second, and uses deterministic
   Gaussian tangent-plane kicks with sigma 10 m/s. Each kick spawns a colored
   Brownian sphere that appears to hit and bounce off the affected kart.
3. `Photon Hit`
   A successful photon hit now triggers the same fullscreen Doppler-style
   effect for both forward-fired and backward-fired hits, without the old black
   and white center scanner hole.
4. `Super Position Cat`
   When a super-position pickup resolves into its hazard outcome, the victim
   gets a heavy cat attachment and a speed drop, acting as the replacement for
   the old negative switch-style punishment.
5. `Warp Bubble`
   A positive attachment rather than a debuff, but still part of the current
   attachment system and HUD icon set.

## Item Weight Notes

Normal race item weights are configured in [data/powerup.xml](data/powerup.xml).
They have been rebalanced for the current Minkowski Kart behavior:

- `Anti-Karticle` is weighted more toward leaders and front-runners because it
  is most useful as a backward-fired pressure tool.
- `Maxwell-Boltzmann` is weighted toward karts chasing the leader, since it
  targets the kart in first place and is pointless to hand to first.
- Non-race modes still retain much closer-to-STK fallback weighting.

## Visual Effects

Current relativistic presentation includes:

- warp-bubble shielding and bubble impacts
- black-hole and projectile effects
- photon Doppler hit effects
- super-position world pulse effects
- time-dilation VFX and Maxwell-Boltzmann Brownian kick spheres
- wormhole portal visuals

## Building The Project

This project uses CMake and Ninja for compilation.

### Prerequisites

- LLVM-MinGW toolchain in `.build-tools/llvm-mingw`
- Ninja in `.build-tools/ninja`

### Standard Build

1. Clone the repository with its tracked `build/`, `.build-tools/`, and
   `dependencies-win-x86_64/` directories intact.
2. From the repo root, run:

```powershell
.\compile.bat
```

3. The executable will be generated at `build\bin\supertuxkart.exe`.

### Dev Build

For normal local iteration, prefer the untracked `build-dev/` directory:

1. Configure once:

```powershell
.\configure-dev.bat
```

2. Build incrementally:

```powershell
.\compile-dev.bat
```

3. Force a clean rebuild when needed:

```powershell
.\compile-dev.bat full
```

4. Clean without rebuilding:

```powershell
.\compile-dev.bat clean
```

5. The dev executable will be generated at `build-dev\bin\supertuxkart.exe`.

### Packaging

The packaging flow is driven by `package.ps1`. It now prefers the current repo
build output from `build-dev\bin` and falls back to `build\bin`, while shipping
the current repo `data/` and `stk-assets/` content.

## Credits

### SuperTuxKart

This project is built on the SuperTuxKart engine.

- Official website: [supertuxkart.net](https://supertuxkart.net)
- License: GPLv3

### OpenRelativity

Relativistic rendering and math ideas are adapted from OpenRelativity by the
MIT Game Lab.

- Original toolkit: [OpenRelativity on GitHub](https://github.com/MITGameLab/OpenRelativity)
- License: MIT

---

Developed as an educational and experimental MinkowskiKart fork.
