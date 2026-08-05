# Minkowski Kart — press summary

**Minkowski Kart** is an experimental, free and open-source 3D relativistic kart-racing game created by **Robson Christie**. Built from SuperTuxKart, it puts selected ideas from special relativity into an arcade race: the in-game speed of light is adjustable, the HUD exposes beta, gamma and proper time, and the view changes as the kart approaches the configured speed limit.

## What makes it different

Rather than using relativity only as a theme, Minkowski Kart links equations and approximations to what the player sees and does. Retarded-position rendering represents light-travel delay, while aberration changes apparent viewing directions. A conditional Doppler-inspired colour effect supplies an additional visual cue. The item system includes a radius-limited time-dilation field, a homing black-hole projectile with lensing, and linked wormholes that teleport racers between endpoints and add a lensing effect.

The game supports keyboard and gamepad input, local split-screen, and LAN play. It does not currently operate a public multiplayer server.

## Scientific scope

Minkowski Kart is a game and experimental visualization, not an exact physics simulator. Its handling uses Bullet-based arcade dynamics with a speed cap, not a complete special-relativistic dynamics model. It does not solve general-relativity field equations, provide a true see-through portal view, apply continuous Doppler colour shifting to all high-speed play, or implement full Lorentz contraction. The black-hole and wormhole mechanics are deliberate gameplay and visual approximations.

## Availability

The latest tagged release is **`version_1.9`**, published **18 July 2026**. Working experimental builds are supported for **Windows, macOS, Linux and Android**, and the project owner confirms that all four run. Windows CI provides race-entry coverage; separately, the exact published Windows x64 ZIP matched its expected SHA-256, passed full CRC and extraction checks, contained the executable, both data trees, and the `Mobius` and `Minkowski` assets, then completed a 44.64-second headless race-initialization smoke with no error or crash markers. Its root `COPYING` file is missing, but the publication workflow update adds it to future packages; the executable is unsigned, and graphical rendering was not independently retested: a Microsoft Defender command-line heuristic blocked the hidden graphical-launch command, but did not detect the game executable itself. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural CI for ARM64 and Android 5 or later; and Linux has owner-confirmed runtime evidence, with a possible missing-`stk-assets` archive concern for redistributors to check. iOS remains an ad-hoc developer build.

There is no gameplay video yet. The repository includes genuine gameplay screenshots in `paper/ajp-minkowski-kart/figures`.

## Credits and licence

Minkowski Kart is derived from [SuperTuxKart](https://github.com/supertuxkart/stk-code) and distributed under GPLv3-or-later, with upstream and asset attributions preserved. Its mathematics and colour-shift work draw inspiration from the MIT-licensed [OpenRelativity](https://github.com/MITGameLab/OpenRelativity) project, which is credited in the source.

## Links

- Website: <https://rchristie95.github.io/MinkowskiKart/>
- Latest tagged release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>
- Source: <https://github.com/rchristie95/MinkowskiKart>
- Contact and bug reports: <https://github.com/rchristie95/MinkowskiKart/issues>

**Suggested one-line description:** Free, open-source 3D relativistic kart racing for Windows, macOS, Linux and Android, with time dilation, aberration and black-hole lensing.
