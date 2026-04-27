# Minkowski Kart: Relativistic Racing

Minkowski Kart is a fork of [SuperTuxKart](https://github.com/supertuxkart/stk-code)
built around special and general relativity. The arcade kart-racing engine is
kept intact, but every item in the original STK item system is replaced with a
spacetime-themed counterpart. Screen effects, debuffs, and HUD icons are all
redesigned to match.

## What Changed From SuperTuxKart

The item system is the main departure from stock STK. Each original item has
been swapped one-for-one with a relativistic equivalent:

| Minkowski Kart powerup | Replaces (original STK) |
|---|---|
| Warp Bubble | Bubblegum |
| Asteroid | Cake |
| Black Hole | Bowling Ball |
| Zipper | Zipper *(unchanged)* |
| Photon | Plunger |
| Super Position | Switch |
| Anti-Karticle | Swatter |
| Wormhole | Rubber Ball |
| Time Dilation *(attachment)* | Parachute *(attachment)* |
| Maxwell-Boltzmann *(attachment)* | Anvil *(attachment)* |

The gameplay logic lives in `src/items/` and item weights are configured in
[data/powerup.xml](data/powerup.xml).

## Powerup Reference

### 1. Warp Bubble *(replaces Bubblegum)*

A defensive shield sphere. Firing forward activates the bubble as a protective
shell and grants a short max-speed boost. Firing backward drops a trap
bubblegum on the track, exactly as in the original STK bubblegum backward-drop.

### 2. Asteroid *(replaces Cake)*

A fast, dense lobbed projectile. Flies in a ballistic arc like the original
cake but with a heavier, rocky appearance and no homing.

### 3. Black Hole *(replaces Bowling Ball)*

A slow, heavy homing projectile. Reuses the bowling ball slot and physics but
renders as a pocket black hole. Homes in on the nearest kart ahead.

### 4. Zipper *(unchanged)*

The standard rocket speed boost from STK. Behaviour and timing are identical
to the original.

### 5. Photon *(replaces Plunger)*

A reworked plunger. Firing forward launches a photon that creates a tether
hit on impact. Firing backward applies the same Doppler-style fullscreen hit
effect directly. The old black-and-white center scanner hole overlay has been
removed from both modes.

### 6. Super Position *(replaces Switch)*

A global item-collapse that still rotates all track pickups, as in the original
STK switch, but pairs it with a relativistic world-pulse VFX. Karts that are
caught in the collapse can receive a heavy cat attachment (see Super Position
Cat below).

### 7. Anti-Karticle *(replaces Swatter)*

A mirrored anti-kart clone launched from the kart. On contact it annihilates
with a pair-production flash. Most useful as a backward-fired pressure tool;
weights are skewed toward leaders and front-runners accordingly.

### 8. Wormhole *(replaces Rubber Ball)*

Spawns a linked pair of traversable spacetime portals. Any kart that drives
into the entry portal exits from the exit portal. Both portals persist for
20 seconds. The rubber ball model is reused for the portal projectile.

### 9. Time Dilation *(replaces Parachute)*

A field-effect powerup that applies a slowdown attachment to every other active
kart. Racers ahead of the user still receive the strongest rank-scaled effect.
The original parachute mesh is hidden but the speed-reduction and VFX remain.

### 10. Maxwell-Boltzmann *(replaces Anvil)*

A leader-punish that targets the kart currently in first place. It runs for
10 seconds and delivers one deterministic Gaussian tangent-plane velocity kick
per second (sigma 10 m/s). Each kick spawns a colored Brownian sphere that
visibly hits and bounces off the affected kart. Weighted away from first place
since it is pointless to hand to the leader.

## Debuffs and Attachments

### Time Dilation *(replaces Parachute attachment)*

The primary slowdown debuff. Applied by the Time Dilation powerup and by
direct hits from banana-style track hazards.

### Maxwell-Boltzmann *(replaces Anvil attachment)*

Applied to the current leader by the Maxwell-Boltzmann powerup. Lasts
10 seconds, starts kicking after 1 second, and uses deterministic Gaussian
tangent-plane kicks.

### Photon Hit

A fullscreen Doppler-style effect triggered on a successful photon impact,
for both forward-fired and backward-fired hits. No scanner-hole overlay.

### Super Position Cat

When a Super Position collapse resolves as a hazard outcome, the victim
receives a heavy cat attachment and a speed drop. This is the negative
counterpart to the original STK negative-switch outcome.

### Warp Bubble *(attachment)*

A positive on-kart attachment representing the active shield. Visible in the
HUD icon set and in the attachment system alongside the debuffs.

## Item Weight Notes

Weights are configured per-rank in [data/powerup.xml](data/powerup.xml) and
rebalanced for Minkowski Kart behavior:

- **Anti-Karticle** skews toward leaders and front-runners (most useful fired
  backward at chasers).
- **Maxwell-Boltzmann** skews toward mid-pack and tail karts (targets first
  place, so giving it to first is wasteful).
- Non-race modes (battle, soccer, FTL) retain closer-to-STK fallback weights.

## Visual Effects

- Warp-bubble shielding and bubble impacts
- Black-hole projectile rendering
- Photon Doppler hit fullscreen flash
- Super Position world-pulse collapse effect
- Time-dilation field VFX and Maxwell-Boltzmann Brownian kick spheres
- Wormhole portal visuals

## Building The Project (Windows)

This project uses CMake and Ninja with the llvm-mingw toolchain.

### 1. Install CMake

```powershell
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
```

### 2. Download llvm-mingw

Download the Windows x86_64 zip from the
[llvm-mingw releases page](https://github.com/mstorsjo/llvm-mingw/releases)
and extract it into `.build-tools\llvm-mingw\` so the layout is:

```
.build-tools\llvm-mingw\llvm-mingw-<date>-msvcrt-x86_64\bin\x86_64-w64-mingw32-clang.exe
```

`compile.bat` specifically expects the `20260407` release. Any recent release
should work if you adjust the path in `compile.bat`.

### 3. Download Ninja

Download `ninja-win.zip` from the
[Ninja releases page](https://github.com/ninja-build/ninja/releases) and
extract `ninja.exe` into `.build-tools\ninja\`.

### 4. Download the Windows dependencies

```bash
curl -L -o deps.zip https://github.com/supertuxkart/dependencies/releases/download/preview/dependencies-win-x86_64.zip
unzip deps.zip
```

This extracts `dependencies-win-x86_64\` into the repo root.

### 5. Configure with CMake

```bash
LLVM_PREFIX=".build-tools/llvm-mingw/llvm-mingw-20260407-msvcrt-x86_64"
NINJA=".build-tools/ninja/ninja.exe"

cmake -S . -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DLLVM_ARCH=x86_64 \
  -DLLVM_PREFIX="$LLVM_PREFIX" \
  -DCMAKE_TOOLCHAIN_FILE="cmake/Toolchain-llvm-mingw.cmake" \
  -DCHECK_ASSETS=OFF \
  -DUSE_DIRECTX=OFF \
  -DUSE_WIIUSE=OFF
```

`-DUSE_WIIUSE=OFF` is required on Windows with llvm-mingw because the WinHID
headers are not bundled. `-DCHECK_ASSETS=OFF` skips the asset presence check
at configure time.

### 6. Build

```bash
.build-tools/ninja/ninja.exe -C build -j4
```

The executable is output to `build\bin\supertuxkart.exe`.

### 7. Post-build setup

Copy the runtime DLLs next to the executable:

```bash
cp dependencies-win-x86_64/bin/*.dll build/bin/
```

Create the replay directory (required by the asset loader):

```bash
mkdir -p data/replay
```

### 8. Run

```bat
run.bat
```

This launches `build\bin\supertuxkart.exe` with `--root-data=../../data`.
The asset loader automatically discovers `stk-assets\` from that path.

### Packaging

The packaging flow is driven by `package.ps1`. It prefers the current repo
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
