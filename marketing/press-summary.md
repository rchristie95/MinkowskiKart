# Minkowski Kart — press summary

**Minkowski Kart** is an experimental, free and open-source 3D relativistic kart-racing game created by **Robson Christie**. Built from SuperTuxKart, it puts selected ideas from special relativity into an arcade race: the in-game speed of light is adjustable, the HUD exposes beta, gamma and proper time, and the view changes as the kart approaches the configured speed limit.

## What makes it different

Minkowski Kart links relativity-inspired equations and visual techniques directly to what the player sees and does. Retarded-position rendering represents light-travel delay, aberration changes apparent viewing directions, and selected gameplay states trigger a Doppler-inspired colour effect. The item system includes a radius-limited time-dilation field, a homing black-hole projectile with lensing, and linked wormholes that teleport racers between endpoints.

The game supports keyboard and gamepad input, local split-screen, and LAN play, with multiplayer focused on player-hosted local networks.

## Scientific scope

Minkowski Kart is a game and experimental visualisation. Bullet-based arcade handling and a speed cap provide the driving model; selected relativity calculations power the telemetry and renderer. Black holes, wormholes, selective Doppler colour shifts, and screen-space lensing are designed as readable gameplay effects.

## Availability

The latest tagged release is **`version_1.9`**, published **18 July 2026**. Working experimental builds are supported for **Windows, macOS, Linux and Android**, and the project owner confirms that all four run. Windows CI provides race-entry coverage; separately, the exact published Windows x64 ZIP matched its expected SHA-256, passed full CRC and extraction checks, contained the executable, both data trees, and the `Mobius` and `Minkowski` assets, then completed a 44.64-second headless race-initialization smoke with no error or crash markers. Its root `COPYING` file is missing, but the publication workflow update adds it to future packages. The executable is unsigned. A Microsoft Defender heuristic blocked the hidden graphical-launch command before rendering could be independently retested; the game executable received no detection. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural CI for ARM64 and Android 5 or later; and Linux has owner-confirmed runtime evidence, with a possible missing-`stk-assets` archive concern for redistributors to check. iOS remains an ad-hoc developer build.

There is no gameplay video yet. The repository includes genuine gameplay screenshots in `paper/ajp-minkowski-kart/figures`.

## Credits and licence

Minkowski Kart is derived from [SuperTuxKart](https://github.com/supertuxkart/stk-code) and distributed under GPLv3-or-later, with upstream and asset attributions preserved. Its mathematics and colour-shift work draw inspiration from the MIT-licensed [OpenRelativity](https://github.com/MITGameLab/OpenRelativity) project, which is credited in the source.

## Links

- Website: <https://rchristie95.github.io/MinkowskiKart/>
- Latest tagged release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>
- Source: <https://github.com/rchristie95/MinkowskiKart>
- Contact and bug reports: <https://github.com/rchristie95/MinkowskiKart/issues>

**Suggested one-line description:** Free, open-source 3D relativistic kart racing for Windows, macOS, Linux and Android, with time dilation, aberration and black-hole lensing.
