# Minkowski Kart: Building a Relativistic Racing Game

_Draft for publication. Project status: experimental._

What changes when the speed limit in a kart racer is not an arbitrary number, but a visible speed of light?

That question motivates Minkowski Kart, an open-source 3D relativistic kart-racing game created by Robson Christie. The project is built from SuperTuxKart, retaining the immediate language of arcade racing—laps, items, local competition and familiar controls—while introducing selected ideas from special relativity into the player's view and telemetry.

The aim is not to turn a race into a complete numerical physics experiment. It is to make reference frames, light-travel delay and clock rates tangible enough to explore at driving speed.

## Bringing the speed of light onto the track

Relativistic effects are normally difficult to notice because everyday speeds are tiny compared with the physical speed of light. Minkowski Kart makes the effective in-game value of `c` adjustable. This allows a kart to reach a substantial fraction of that value without requiring an astronomical track.

The central quantities are:

```text
beta  = v / c
gamma = 1 / sqrt(1 - beta^2)
```

Here `v` is the kart's coordinate speed in the race frame. The game exposes beta and gamma alongside speed and the chosen value of `c`. It also accumulates proper time for the kart:

```text
d(proper time) = d(coordinate time) / gamma
```

Race timing still belongs to the shared coordinate frame. Proper time is maintained as separate telemetry, which lets a player compare the race clock with the time accumulated by the moving kart without replacing ordinary arcade timing rules.

The configured speed cap prevents a kart from crossing the in-game value of `c`. That creates a playable limit, but it is important to describe the model precisely: Minkowski Kart still uses arcade handling and Bullet-based dynamics. It is not a full special-relativistic treatment of vehicle dynamics.

## Rendering what an observer receives

A naive “relativistic” effect might simply squash scenery in the direction of travel. Minkowski Kart takes a different route. Its visual system works with the observer's velocity and applies two related ideas: retarded position and aberration.

A retarded position asks where an object was when the light now reaching the observer would have left it. This introduces an emission-time or light-travel-delay component rather than displaying every object only at its current race-frame position. Aberration then changes the apparent direction of the incoming ray according to the observer's motion.

The implementation keeps an observer state containing values such as the effective `c`, beta, gamma, inverse gamma, observer position and velocity direction. That state is supplied to the rendering path so geometry can be presented using the same relativistic inputs. The relevant code is grouped under [`src/relativity`](../src/relativity/), with shader-side visual transforms under [`data/shaders/utils`](../data/shaders/utils/).

Colour is handled more cautiously than the phrase “Doppler shifting everywhere” would imply. Minkowski Kart includes a Doppler-inspired colour-shift path, but it is conditional: it is activated by particular gameplay states rather than continuously applied throughout all high-speed driving. Any public description should preserve that distinction.

## Making relativity playable

Telemetry helps explain the model, but racing also needs decisions. Several items turn spacetime ideas into bounded mechanics:

- **Time dilation** sends out a limited-radius effect that changes affected rivals rather than globally slowing every kart.
- **Black hole** is a homing projectile accompanied by a lensing effect.
- **Wormhole** creates linked endpoints. A kart entering one is teleported to the other, with lensing used to communicate the connection.

These are deliberately legible game systems. The black hole does not arise from a numerical solution of Einstein's field equations, and the wormhole is not a true portal renderer showing a live view of its destination. The names and visuals connect the item design to physics, while the implementation remains suitable for an arcade race.

The game supports keyboard and gamepad input, local split-screen, and LAN play. A hosted public multiplayer service is not currently deployed.

## Architecture inherited and extended

Starting from [SuperTuxKart](https://github.com/supertuxkart/stk-code) supplied a mature racing-game foundation: the project could concentrate on the relativistic state, rendering transforms, telemetry and item behavior while preserving recognizable kart handling. Minkowski Kart remains GPLv3-or-later and retains the associated upstream and asset attribution requirements.

[OpenRelativity](https://github.com/MITGameLab/OpenRelativity), an MIT Game Lab project released under the MIT License, provided mathematical and colour-shift inspiration. Minkowski Kart credits that relationship rather than presenting the relevant ideas as wholly independent. SuperTuxKart is the engine foundation; OpenRelativity is an attributed technical influence.

At a high level, the project separates three concerns:

1. **Race and kart state** supplies coordinate velocity and ordinary arcade simulation.
2. **Relativity state** derives beta, gamma and proper-time telemetry from the configured `c` and the kart's motion.
3. **Visual and item systems** consume that state for retarded-position and aberration rendering, conditional colour treatment, lensing, teleportation and local time-dilation effects.

This separation is useful because not every clock or rule should run in the kart's proper time. Keeping race time separate avoids silently changing unrelated gameplay systems while still making the distinction visible to players.

## Boundaries and technical challenges

The largest design challenge is not adding as many equations as possible. It is deciding where an equation produces a stable, readable racing mechanic.

Visual distortion has to remain understandable while scenery, rivals and the observer move. Item effects need spatial limits and clear feedback. Multiplayer modes need the same configured rules, yet the project should not imply a public service where none exists. Release packaging must also carry a large inherited asset set and its licence records; a binary without the matching data is not a usable build.

The current scientific boundaries are therefore part of the design record:

- dynamics remain arcade-oriented and speed-capped;
- no general-relativity field equations are solved;
- black-hole and wormhole behavior is approximate;
- wormholes teleport but do not provide a true portal view;
- the Doppler-inspired colour effect is conditional;
- full Lorentz contraction is not implemented.

Stating these limits does not diminish the experiment. It tells players and educators exactly which observations the software can support.

## Building, testing and contributing

The source is available from the [Minkowski Kart repository](https://github.com/rchristie95/MinkowskiKart). The current Windows build uses CMake, Ninja and an LLVM-MinGW toolchain; follow the repository's current [build instructions](https://github.com/rchristie95/MinkowskiKart#build-from-source) rather than copying an old command from a third-party post. The repository also contains the data and `stk-assets` trees required at runtime.

Useful starting points for contributors include the relativity math and observer state under `src/relativity`, the black-hole and wormhole implementations under `src/items`, and the rendering integration under `src/graphics` and `data/shaders`. Read the project-specific [contribution guide](https://github.com/rchristie95/MinkowskiKart/blob/main/CONTRIBUTING.md) before proposing a change. Contributions should preserve the GPLv3-or-later licence, OpenRelativity attribution and asset-specific licence files.

Reproducible reports are especially valuable for experimental software. Include the exact revision or release, operating system, graphics hardware, controller type, steps to reproduce and relevant logs in a [GitHub issue](https://github.com/rchristie95/MinkowskiKart/issues).

The latest tagged release is [`version_1.9`](https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9), dated 18 July 2026. Working builds are supported for Windows, macOS, Linux and Android, and the project owner confirms that all four run. Windows CI enters a race; separately, the exact published Windows x64 ZIP matched its expected SHA-256, passed CRC, extraction and content checks, and its executable completed a 44.64-second headless race-initialization smoke with no error or crash markers. Root `COPYING` is missing from that archive but the publication workflow update adds it to future packages; the executable is unsigned, and a Defender command-line heuristic—not a game detection—prevented an independent graphical retest. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural CI; and Linux has owner-confirmed runtime evidence with a possible missing-`stk-assets` archive concern. iOS remains developer/ad-hoc. The project is playable, while its experimental status and test boundaries remain visible.

Learn more at <https://rchristie95.github.io/MinkowskiKart/>.
