# Minkowski Kart — Relativistic Kart Racing

## Project metadata

| Field | Value |
|---|---|
| Classification | Game |
| Kind | Downloadable |
| Genre | Racing |
| Pricing | Free / no payments |
| Status | In development; describe publicly as **experimental** |
| Platforms | Windows, macOS, Linux and Android |
| Creator | Robson Christie |
| Licence | GPLv3-or-later; includes SuperTuxKart and asset attributions |
| Website | <https://rchristie95.github.io/MinkowskiKart/> |
| Download notes | <https://rchristie95.github.io/MinkowskiKart/download.html> |
| Source | <https://github.com/rchristie95/MinkowskiKart> |
| Issues | <https://github.com/rchristie95/MinkowskiKart/issues> |

## Short description

Free, open-source 3D relativistic kart racing for Windows, macOS, Linux and Android, with time dilation, aberration and black-hole lensing.

## Page copy

Minkowski Kart is an experimental, open-source 3D relativistic kart-racing game created by Robson Christie and built from SuperTuxKart.

Bring the in-game speed of light within reach, then watch beta, gamma and your kart's proper-time clock change as you race. Retarded-position and aberration effects reshape the view. A conditional Doppler-inspired colour effect adds another visual cue.

### Features

- Adjustable in-game speed of light
- Beta, gamma, speed and proper-time telemetry
- Retarded-position and aberration visuals
- Radius-limited time-dilation power-up
- Homing black-hole projectile with lensing
- Linked wormholes with teleportation and lensing
- Keyboard and gamepad controls
- Local split-screen and LAN play

### Experimental status

Minkowski Kart is a playable physics experiment built around responsive Bullet-based arcade handling and a relativistic speed cap. Black holes, wormholes, selective Doppler colour shifts, and apparent-position effects turn relativity-inspired ideas into racing mechanics.

Working experimental builds are supported for Windows, macOS, Linux and Android, and the project owner confirms that all four run. Windows CI enters a race; separately, the exact published Windows ZIP's SHA-256 matched the expected value, CRC and extraction passed, `MinkowskiKart.exe`, both data trees, `Mobius` and `Minkowski` were present, and the executable completed a 44.64-second headless race-initialization smoke without error or crash markers. It is unsigned and root `COPYING` is missing; the publication workflow update adds it to future packages. A Defender heuristic blocked the hidden graphical-launch command before rendering could be retested; the game executable received no detection. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural checks for ARM64 and Android 5+; and Linux has owner-confirmed runtime evidence, with a possible missing-`stk-assets` archive concern. iOS remains developer/ad-hoc. Multiplayer focuses on local split-screen and player-hosted LAN sessions.

### Install

1. Download the package for Windows, macOS, Linux or Android.
2. On desktop, extract the complete package and keep its `data`, `stk-assets`, licence and attribution files together. On Linux, confirm that `stk-assets` is present.
3. Launch the packaged Windows, macOS or Linux application. On ARM64 Android 5 or later, install and launch the APK through Android's normal package-install flow.

Find the project page at <https://rchristie95.github.io/MinkowskiKart/> and read the current [download and platform notes](https://rchristie95.github.io/MinkowskiKart/download.html). Read the source or contribute at <https://github.com/rchristie95/MinkowskiKart>. Report bugs and build results at <https://github.com/rchristie95/MinkowskiKart/issues>.

### Licence and credits

Minkowski Kart is GPLv3-or-later and derived from [SuperTuxKart](https://github.com/supertuxkart/stk-code). Its mathematical and colour-shift work was inspired by the MIT-licensed [OpenRelativity](https://github.com/MITGameLab/OpenRelativity) project. Upstream and asset-specific attribution files are included with the source and must remain with distributions.

## Tags

Use: `Racing`, `Kart racing`, `Physics`, `Science`, `Educational`, `Open source`, `3D`, `Sci-fi`, `Special relativity`.

## Genuine screenshot captions

1. **Race view and telemetry** — “Racing on the Möbius track with speed, beta, gamma, effective speed of light and proper-time telemetry visible.” Source: `paper/ajp-minkowski-kart/figures/mobius_race_hud.png`.
2. **Black-hole lensing** — “A black-hole item produces a lensing effect during a race.” Source: `paper/ajp-minkowski-kart/figures/mobius_lensing.png`.
3. **High-beta view** — “Track geometry appears distorted by retarded-position and aberration effects at high beta.” Source: `paper/ajp-minkowski-kart/figures/mobius_high_beta_warp.png`.

Do not add accessibility tags until an accessibility audit has verified them. Do not use an `Online multiplayer` tag while only LAN play and no public server are available.
