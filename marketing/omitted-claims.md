# Claims deliberately omitted or qualified

This file records publication boundaries for Minkowski Kart. Revisit a claim only when new evidence is available.

| Claim not used | Reason | What would make it publishable |
|---|---|---|
| “Stable,” “public alpha” or “production ready” | The verified status is **experimental**. | A documented release policy and broader testing. |
| “Identical automated gameplay coverage on every supported platform” | The owner confirms that Windows, macOS, Linux and Android builds run, but evidence differs. Windows has CI race-entry plus an exact-archive 44.64-second headless smoke; macOS has headless CI; Android has structural CI; and Linux has no equivalent automated race-entry smoke. | Add comparable packaged-build gameplay automation on each supported platform. |
| “The Linux archive is packaging-audit clean” | Linux is owner-confirmed running, but an audit raised a possible missing-`stk-assets` concern in the archive. This is a packaging caveat, not a lack of Linux support. | Confirm `stk-assets` in the exact published archive and record the package check. |
| General iOS player support | The current iOS package is developer/ad-hoc. | Document signing and distribution requirements, then test a generally installable player package. |
| “All release archives have verified checksums” | The exact Windows x64 `version_1.9` archive's SHA-256 matched the expected value; the other platform archives were not included in that checksum audit. | Verify and publish a checksum for every exact release asset. |
| “The Windows executable is signed” | The published `version_1.9` executable is unsigned. | Apply and verify an appropriate code signature in the release workflow. |
| “Every release archive includes the root GPL licence” | The exact Windows archive is missing root `COPYING`, although its executable, data trees and named game assets are present. The publication workflow now adds it to future packages. | Run the updated workflow and re-audit the resulting archive. |
| “Windows graphics were independently retested in the archive audit” | The exact executable passed a 44.64-second headless race-initialization smoke without error or crash markers. A Defender command-line heuristic blocked the hidden graphical-launch command, but did not detect the game executable, so rendering was not independently retested. | Run and document a visible graphical smoke test of the exact published executable. |
| “Scientifically exact” or “complete relativity simulation” | Driving uses arcade/Bullet dynamics with a speed cap, not full special-relativistic dynamics. | This should remain out unless the simulation architecture materially changes and is independently validated. |
| “Simulates general relativity” | Black holes, wormholes and lensing are gameplay/visual approximations; no GR field equations are solved. | Implement and validate an appropriate GR model, with precise scope stated. |
| Continuous relativistic Doppler rendering | The colour effect is conditional rather than continuously active across all high-speed play. | Implement and test a continuous rendering path. |
| Full Lorentz contraction | It is not implemented. | Implement, document and verify it. |
| True portal rendering | Wormholes teleport and use lensing, but do not show the destination through the opening. | Implement and test a portal-view renderer. |
| Public online multiplayer | Split-screen and LAN are present, but no public server is deployed. | Deploy, monitor and document a public service. |
| Gameplay trailer or video | No genuine footage is currently available. | Record representative gameplay and publish it. |
| Performance, frame-rate or hardware requirements | No verified benchmark or minimum specification is available. | Test defined hardware and publish a reproducible matrix. |
| Accessibility feature support | No verified accessibility inventory is available. | Audit controls, visuals, audio and assistive-technology behavior. |
| Store concept art as gameplay | The repository contains genuine screenshots elsewhere; concept images are not captured play. | Label concept work clearly and establish its provenance, or use genuine screenshots. |
| Independently relicensable project logo | Square icon files exist, but the custom logo's standalone provenance is ambiguous. | Record the source, author and applicable licence before distributing it separately as marketing art. |

## Safe wording

- Say **“experimental relativistic kart-racing game.”**
- Say **“Working experimental builds are supported for Windows, macOS, Linux and Android, and the project owner confirms that all four run.”**
- State CI evidence separately: Windows race-entry smoke, macOS headless CI, Android structural CI and owner-confirmed Linux runtime evidence.
- For Windows `version_1.9`, state that SHA-256, CRC, extraction, required-content and 44.64-second headless race-init checks passed; also state that root `COPYING` is absent, the executable is unsigned and graphical rendering was not independently retested.
- Keep the Linux `stk-assets` packaging concern visible without calling Linux unsupported; keep iOS labelled developer/ad-hoc.
- Describe individual implemented effects and state their approximations.
- Refer readers to the [website](https://rchristie95.github.io/MinkowskiKart/), [`version_1.9` release](https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9), [source](https://github.com/rchristie95/MinkowskiKart) and [issue tracker](https://github.com/rchristie95/MinkowskiKart/issues).
