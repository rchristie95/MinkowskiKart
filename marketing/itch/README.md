# itch.io publication package

This directory contains publication-ready copy for an itch.io project named **Minkowski Kart — Relativistic Kart Racing**. No itch.io page or upload is assumed to exist yet.

## Before publishing

1. Download the exact Windows x64, macOS, Linux x86-64 and Android assets from the [`version_1.9` release](https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9). The owner has confirmed that builds for all four platforms run.
2. Inspect each desktop package in a clean directory and confirm that its executable or application, runtime files, `data`, `stk-assets`, GPL licence and asset attributions are present. The current Windows archive is missing root `COPYING`, and the Linux archive has a possible missing-`stk-assets` concern; correct either affected package before mirroring it.
3. Record the evidence accurately. Windows CI enters a race; separately, the exact published Windows ZIP's SHA-256 matched the expected value, full CRC and extraction passed, `MinkowskiKart.exe`, `data`, `stk-assets`, `Mobius` and `Minkowski` were present, and the exact executable completed a 44.64-second headless race-initialization smoke without error or crash markers. It is unsigned. A Defender command-line heuristic blocked the hidden graphical-launch command, but did not detect the game executable, so rendering was not independently retested. macOS has owner-confirmed gameplay plus headless CI; Linux has owner-confirmed runtime evidence; Android has owner-confirmed runtime evidence plus structural CI for ARM64 and Android 5+.
4. After any packaging correction, repeat the affected archive's integrity, content and launch checks. Generate and publish a SHA-256 checksum for every final uploaded file.
5. Upload the four working packages and select the corresponding **Windows**, **macOS**, **Linux** and **Android** platform flags. Do not select iOS; its current package is developer/ad-hoc.
6. Add the three prepared, genuine gameplay images from [`screenshots/`](screenshots/), with the supplied captions. Do not label store concept art as gameplay.
7. If footage has been captured using [`marketing/video/capture-checklist.md`](../video/capture-checklist.md), add the published video. Otherwise leave the trailer field empty.

## Manual itch.io setup

1. Sign in to itch.io and choose **Dashboard → Create new project**.
2. Paste the title, metadata, description and tags from [`project-copy.md`](project-copy.md).
3. Set the project type to **Downloadable**, classification to **Game**, genre to **Racing**, release status to **In development**, and pricing to **No payments**.
4. Set the canonical website to <https://rchristie95.github.io/MinkowskiKart/> and add the source repository to the description.
5. Upload the four prepared files, apply only the matching platform flag to each, and label them `Minkowski Kart version_1.9 — [platform]`.
6. Upload the genuine screenshots with the supplied captions. Use `screenshots/minkowski-kart-relativistic-racing-game.jpg` as the first image unless a newer verified capture is available, and use `minkowski-kart-social-preview.png` as the project card where itch.io permits it.
7. Keep the page restricted or draft while previewing every link, image and download.
8. Download each itch.io-hosted file once, compare its SHA-256 checksum with the published value, and confirm that the download opens or installs as expected on its named platform.
9. Make the page public only after the hosted files pass those checks. Add the final itch.io URL to the website, README, release notes and video description.

## Current blockers

- Browser authentication and any two-factor or CAPTCHA step require the account owner.
- The hosted copies and their checksums must be verified after upload; this is a publication-integrity step, not a claim that the four supported builds do not run.
- The current Windows package lacks root `COPYING`; the publication workflow update adds it to future packages. Its executable is unsigned.
- The Linux archive's possible missing-`stk-assets` packaging concern must be checked before it is mirrored.
- No gameplay video exists.
- No verified minimum hardware specification or accessibility feature inventory is available.

Report release or build problems at <https://github.com/rchristie95/MinkowskiKart/issues>.
