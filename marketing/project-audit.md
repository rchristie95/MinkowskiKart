# Minkowski Kart publication audit

_Internal snapshot prepared 5 August 2026. Public claims should stay within the evidence summarized here._

## Identity and status

- The public project name is **Minkowski Kart** and the creator's public name is **Robson Christie**.
- Minkowski Kart is an experimental 3D relativistic kart-racing game derived from SuperTuxKart.
- The latest tagged release is [`version_1.9`](https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9), published 18 July 2026.
- The canonical project page is <https://rchristie95.github.io/MinkowskiKart/>.

## Verified player-facing features

- An adjustable in-game speed of light, with beta, gamma and proper-time telemetry.
- Apparent-position rendering using retarded positions and aberration.
- A conditional Doppler-inspired colour effect; it is not a continuous, universal high-speed colour simulation.
- A radius-limited time-dilation power-up.
- A homing black-hole projectile with a lensing effect.
- Linked wormholes with teleportation and lensing; they do not render a true view through the destination portal.
- Local split-screen and LAN play.
- Keyboard and gamepad input.

## Builds and media

- On 5 August 2026, the project owner confirmed that the Windows, macOS, Linux and Android builds run. These four are supported working platforms for the experimental release.
- Evidence is separate and uneven: Windows has automated race-entry and exact-release-archive smoke evidence; macOS has owner-confirmed gameplay plus headless CI; Linux has owner-confirmed runtime evidence but no equivalent automated race-entry smoke; and Android has owner-confirmed runtime evidence plus structural CI for ARM64 and Android 5+.
- The exact published Windows x64 `version_1.9` ZIP's SHA-256 matched the expected value, and a full ZIP CRC test and clean extraction passed. `MinkowskiKart.exe`, `data`, `stk-assets`, and the `Mobius` and `Minkowski` assets were present. That published executable completed a 44.64-second headless race-initialization smoke test with no error or crash markers.
- Windows archive caveats: root `COPYING` is missing from the current archive, the publication workflow update adds it to future packages, and the executable is unsigned. An attempted hidden graphical launch was blocked by a Microsoft Defender command-line heuristic—not a detection of the game executable—so graphical rendering was not independently retested in this audit.
- The Linux build runs, but a packaging audit raised a possible missing-`stk-assets` concern. Check that directory in any redistributed Linux archive; do not turn the packaging concern into a claim that Linux itself is unsupported.
- The iOS package remains an ad-hoc developer build and is not presented as a generally supported player platform.
- No gameplay video is tracked.
- Genuine gameplay screenshots are available under [`paper/ajp-minkowski-kart/figures`](../paper/ajp-minkowski-kart/figures/). Store concept art should not be labelled as gameplay.
- Square project and Android icons exist, including `data/gui/icons/logo_mk.png`, but the custom logo's standalone provenance is ambiguous. Do not treat it as independently relicensable marketing artwork until that provenance is clarified; no verified horizontal wordmark is available.
- Build and packaging automation exists. Owner confirmation establishes that builds run on the four supported platforms; only macOS is specifically recorded here as owner-confirmed gameplay. CI coverage should be described at its actual level rather than as equivalent graphical automation everywhere.

## Scientific scope

The game combines arcade handling and Bullet physics with a speed cap and relativistic presentation. It is not a complete special-relativistic dynamics simulation. The black hole, wormhole and lensing mechanics are gameplay and visual approximations: the project does not solve general-relativity field equations. It also does not implement full Lorentz contraction.

## Licence and attribution

The project retains SuperTuxKart's GPLv3-or-later licensing and asset attribution requirements. OpenRelativity supplied MIT-licensed mathematical and colour-shift inspiration and must remain credited. Release packages must preserve the root licence and the individual asset licence records.

## Publication decision

Describe the project as **experimental**, not alpha or stable. List Windows, macOS, Linux and Android as supported working platforms, and distinguish owner-confirmed runtime evidence from platform-specific CI coverage. Keep the Linux asset-packaging concern visible without describing Linux as unsupported. Treat iOS as developer/ad-hoc, make the source and issue tracker prominent, and do not promise a public multiplayer server.
