# Minkowski Kart `version_1.9`

_Released 18 July 2026 · Experimental_

Minkowski Kart is a free, open-source 3D relativistic kart-racing game created by Robson Christie and built from SuperTuxKart. `version_1.9` exposes an adjustable in-game speed of light with beta, gamma and proper-time telemetry, adds retarded-position and aberration visuals, and turns physics ideas into racing items.

## Included features

- Adjustable effective speed of light
- Speed, beta, gamma and proper-time telemetry
- Retarded-position and aberration rendering
- Conditional Doppler-inspired colour effect
- Radius-limited time-dilation power-up
- Homing black-hole projectile with lensing
- Linked wormhole teleportation with lensing
- Keyboard and gamepad controls
- Local split-screen and LAN play

## Platform status

| Package | Verification status |
|---|---|
| Windows x64 | Supported and owner-confirmed running. CI provides race-entry coverage; the exact published archive separately passed integrity, contents and a 44.64-second headless race-initialization smoke test. |
| macOS | Supported with owner-confirmed gameplay. CI coverage is headless. |
| Linux x86-64 | Supported and owner-confirmed running. A packaging audit raised a possible missing-`stk-assets` concern, so check that directory when redistributing the archive. |
| Android ARM64 | Supported on Android 5 or later and owner-confirmed running. CI coverage is structural. |
| iOS | Ad-hoc developer package; not presented as a generally supported player release. |

The Windows, macOS, Linux and Android builds work, and the project remains experimental. Owner-confirmed runtime evidence and CI coverage are listed separately for clarity. Multiplayer focuses on local split-screen and player-hosted LAN sessions.

### Windows x64 archive verification

- The downloaded `version_1.9` ZIP's SHA-256 matched the expected value.
- Windows x64 SHA-256: `60197bc29f5cdb8420c3dbdec08c9d25d6ba29af5b3505acde77dc09345d971a`
- A full ZIP CRC test and extraction passed.
- `MinkowskiKart.exe`, `data`, `stk-assets`, and the `Mobius` and `Minkowski` assets were present.
- The exact published executable completed a 44.64-second headless race-initialization smoke test with no error or crash markers.
- The archive is missing root `COPYING`; the publication workflow now adds it to future packages. The executable is unsigned.
- A Microsoft Defender heuristic blocked the hidden graphical-launch command and prevented an independent rendering retest. The game executable received no detection.

## Installation

Download the matching asset from the [`version_1.9` release](https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9).

- **Windows:** Extract the complete Windows x64 archive to a new folder, keep `data` and `stk-assets` beside the launcher, and run `run_game.bat`.
- **macOS:** Extract the complete macOS package and launch the packaged application. Keep its accompanying data and licence files intact.
- **Linux:** Extract the complete Linux archive, confirm that both `data` and `stk-assets` are present, then launch the packaged executable. If `stk-assets` is absent, report the exact asset name and use the [source-build instructions](https://github.com/rchristie95/MinkowskiKart#build-from-source) while the archive is corrected.
- **Android:** On an ARM64 device running Android 5 or later, install the APK through Android's normal package-install flow. The AAB is intended for distribution tooling rather than direct installation.

The iOS asset is for ad-hoc developer use and is not included in the general player installation steps.

## Known limitations

- Driving uses arcade/Bullet dynamics with a relativistic speed cap.
- Black holes, wormholes and lensing turn curved-spacetime inspiration into gameplay and visual effects.
- Wormholes teleport between linked endpoints and use distortion to signal the shortcut.
- The Doppler-inspired colour effect activates during selected gameplay states.
- Full Lorentz contraction is not implemented.
- No gameplay video is available for this release.
- No minimum hardware specification or accessibility feature inventory has been verified.
- The Windows release archive is missing root `COPYING`, and its executable is unsigned.
- Windows graphical rendering was not independently retested during the archive audit.

## Integrity and feedback

The exact Windows x64 archive's SHA-256 matched the expected value. When the workflow produces a corrected archive containing root `COPYING`, it also generates and publishes the replacement digest. This audit did not independently recalculate checksums for the other platform archives.

The current Windows digest is also available as [`MinkowskiKart-version_1.9-win.zip.sha256`](https://github.com/rchristie95/MinkowskiKart/releases/download/version_1.9/MinkowskiKart-version_1.9-win.zip.sha256). The updated workflow replaces that sidecar whenever it replaces the Windows archive.

Report bugs and package results in the [issue tracker](https://github.com/rchristie95/MinkowskiKart/issues). Include the release asset name, operating system, graphics hardware, input device, reproduction steps and logs.

## Genuine gameplay screenshots

![Two karts racing on Möbius Trip with velocity, beta, proper-time, and effective-light-speed telemetry](https://raw.githubusercontent.com/rchristie95/MinkowskiKart/main/paper/ajp-minkowski-kart/figures/mobius_race_hud.png)

![A race scene distorted by the game's analytic black-hole lensing effect](https://raw.githubusercontent.com/rchristie95/MinkowskiKart/main/paper/ajp-minkowski-kart/figures/mobius_lensing.png)

![The Möbius Trip track distorted by retarded-position and aberration effects at high beta](https://raw.githubusercontent.com/rchristie95/MinkowskiKart/main/paper/ajp-minkowski-kart/figures/mobius_high_beta_warp.png)

Website: <https://rchristie95.github.io/MinkowskiKart/>

Source: <https://github.com/rchristie95/MinkowskiKart>

Minkowski Kart is GPLv3-or-later and derived from SuperTuxKart. Its mathematical and colour-shift work was inspired by the MIT-licensed OpenRelativity project. Distributions must retain the project licence, upstream credits and asset-specific attribution files.
