# Minkowski Kart announcement drafts

These drafts describe Minkowski Kart as **experimental**. Retain that wording unless the release process and project owner establish a new status.

Canonical links:

- Website: <https://rchristie95.github.io/MinkowskiKart/>
- Current release (`version_1.9`): <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>
- Source: <https://github.com/rchristie95/MinkowskiKart>
- Issues: <https://github.com/rchristie95/MinkowskiKart/issues>

## itch.io devlog

### Introducing Minkowski Kart: experimental relativistic kart racing

Minkowski Kart is an experimental, open-source 3D relativistic kart-racing game. It builds on SuperTuxKart and explores what happens when ideas from relativity become part of an arcade race.

You can adjust the game’s effective speed of light and follow beta, gamma, and proper-time telemetry while racing. The renderer includes retarded-position and aberration effects, with a conditional Doppler colour effect. The item set turns related ideas into arcade mechanics: a radius-limited time-dilation field affects nearby opponents, black holes home and distort the image, and linked wormholes teleport karts between two endpoints.

This is a game experiment, not a complete or scientifically exact simulation. Kart dynamics remain arcade-style and speed-capped, the project does not solve general-relativity equations, and its black-hole and wormhole effects are gameplay representations. Split-screen and LAN play are included; there is not currently a project-operated public game server.

Working `version_1.9` builds are supported for Windows, macOS, Linux and Android; the project owner confirms that all four run. Windows CI enters a race, and the exact published Windows ZIP's SHA-256 separately matched the expected value; CRC, extraction, content and a 44.64-second headless race-initialization smoke also passed. Its root `COPYING` is missing, the executable is unsigned and graphical rendering was not independently retested. macOS has owner-confirmed gameplay plus headless CI; Linux has owner-confirmed runtime evidence; and Android has owner-confirmed runtime evidence plus structural CI. A Linux `stk-assets` packaging concern remains under audit.

- Project website: <https://rchristie95.github.io/MinkowskiKart/>
- Current release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>
- Source code: <https://github.com/rchristie95/MinkowskiKart>
- Bug reports and feedback: <https://github.com/rchristie95/MinkowskiKart/issues>

Minkowski Kart is licensed under GPLv3-or-later, inherited from its SuperTuxKart foundation. OpenRelativity’s MIT-licensed colour-shift and mathematics work is credited as an inspiration.

## SuperTuxKart community forum

### Minkowski Kart: an experimental relativity-focused SuperTuxKart fork

Hello! I’m sharing Minkowski Kart, an experimental GPLv3-or-later project built from SuperTuxKart.

The fork keeps arcade kart racing at its centre while adding a relativity-focused layer: an adjustable effective speed of light; beta, gamma, and proper-time telemetry; retarded-position and aberration rendering; a conditional Doppler colour effect; and spacetime-themed items. The time-dilation item acts within a limited radius, black holes combine a homing projectile with lensing, and paired wormholes teleport karts and add a lensing effect. Split-screen and LAN racing are present, alongside keyboard and gamepad controls.

I want to be precise about scope: this is experimental game design, not full special-relativistic vehicle dynamics or a general-relativity solver. The karts still use speed-capped arcade physics, and the portal and gravitational visuals are approximations made for play.

Working builds are supported for Windows, macOS, Linux and Android, and the owner confirms that all four run. Windows has CI race-entry coverage; the exact published Windows ZIP also passed integrity, extraction, content and a 44.64-second headless race-initialization smoke, although it is unsigned, lacks root `COPYING` and was not graphically retested. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural CI; and Linux has owner-confirmed runtime evidence with an asset-packaging concern to check.

Thank you to the SuperTuxKart contributors whose work provides the project’s foundation. OpenRelativity’s MIT-licensed colour-shift and mathematics work is also credited as an inspiration.

Project: <https://rchristie95.github.io/MinkowskiKart/>

Source: <https://github.com/rchristie95/MinkowskiKart>

Experimental release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>

Issues: <https://github.com/rchristie95/MinkowskiKart/issues>

Feedback from people familiar with SuperTuxKart would be especially useful: do the new visual effects and items communicate their behaviour clearly during a race?

## Indie-game community

### I’m making an experimental kart racer where the speed of light is a game setting

Minkowski Kart is an open-source 3D kart racer that brings relativity-inspired rules and visuals into arcade racing. Players can change the effective speed of light, see beta/gamma and proper-time telemetry, and encounter time-dilation fields, homing black holes, and paired wormholes during a race. Retarded-position, aberration, lensing, and conditional Doppler colour effects alter how the action looks.

It is deliberately an experimental game rather than a claim of scientific exactness. The driving model is still speed-capped arcade physics, and there is no project-operated public server; local split-screen and LAN play are available. The owner confirms that supported Windows, macOS, Linux and Android builds all run. The exact Windows release passed integrity, content and a 44.64-second headless race-init smoke; it remains unsigned, lacks root `COPYING` and was not graphically retested. CI coverage differs elsewhere, and the Linux archive still has a possible missing-`stk-assets` concern.

If the premise sounds interesting, the project overview and download notes are here: <https://rchristie95.github.io/MinkowskiKart/>

The code is public under GPLv3-or-later: <https://github.com/rchristie95/MinkowskiKart>

Current release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>

Feedback and reproducible bug reports: <https://github.com/rchristie95/MinkowskiKart/issues>

I’d particularly value feedback on whether the telemetry and visual effects are readable while you are also trying to race.

## Physics or science-game community

### Relativity as a kart-racing mechanic: Minkowski Kart

Minkowski Kart is an experimental, open-source relativistic kart-racing game built on SuperTuxKart. Its aim is to make reference-frame ideas visible and playable without presenting the result as a complete physics simulation.

The player can adjust the effective speed of light and inspect beta, gamma, and proper-time telemetry. Rendering code applies retarded-position and aberration effects and can apply a Doppler colour effect under supported conditions. Relativity also becomes game logic: a time-dilation item affects other karts inside a limited radius, black holes act as homing projectiles with lensing, and linked wormholes teleport karts with a lensing effect.

The boundaries matter. Vehicle motion remains Bullet-based, speed-capped arcade dynamics rather than full special-relativistic dynamics. Minkowski Kart does not solve general-relativity equations, render a true view through a portal, apply full Lorentz contraction, or show continuous high-speed Doppler shifting in every rendering path. The black holes and wormholes are game mechanics and visual approximations.

Working experimental builds are supported for Windows, macOS, Linux and Android, and the owner confirms that all four run. Windows has race-entry CI plus exact-archive integrity, content and 44.64-second headless race-init evidence; graphical rendering was not independently retested. macOS has owner-confirmed gameplay plus headless CI; Android has owner-confirmed runtime evidence plus structural CI; and Linux has owner-confirmed runtime evidence with a packaging concern around `stk-assets`.

That trade-off is the discussion I’d most like to have: which cues best help a player build intuition, and which risk suggesting more fidelity than the implementation provides?

- Explanation and downloads: <https://rchristie95.github.io/MinkowskiKart/>
- Source: <https://github.com/rchristie95/MinkowskiKart>
- Current experimental release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>
- Technical questions and bugs: <https://github.com/rchristie95/MinkowskiKart/issues>

The project is GPLv3-or-later and credits OpenRelativity’s MIT-licensed colour-shift and mathematics work as an inspiration.

## Mastodon

Minkowski Kart is an experimental open-source relativistic kart-racing game for Windows, macOS, Linux and Android. Adjust the speed of light, watch beta/gamma and proper time, and race with time-dilation fields, homing black holes and linked wormholes.

Arcade physics and visual approximations—not full SR/GR simulation.

Project: https://rchristie95.github.io/MinkowskiKart/
Release: https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9

#IndieGame #OpenSource #Physics

## Bluesky

Experimental open-source relativistic kart-racing game for Windows, macOS, Linux & Android. Adjust c, watch beta/gamma and proper time, and race with time-dilation fields, black holes & wormholes. Arcade game, not full SR/GR.

https://rchristie95.github.io/MinkowskiKart/

Suggested first reply:

Source: https://github.com/rchristie95/MinkowskiKart

Experimental `version_1.9` release: https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9

Bugs and feedback: https://github.com/rchristie95/MinkowskiKart/issues

Suggested second reply:

Windows’s exact ZIP passed SHA/CRC/content checks + a 44.64s headless race-init; it is unsigned, lacks root `COPYING`, and was not graphically retested. macOS gameplay is owner-confirmed + headless CI; Android has structural CI; Linux has an asset-packaging concern. iOS is developer/ad-hoc.

## Reddit

### I’ve been experimenting with relativity as a kart-racing mechanic

I’m Robson Christie, the creator of Minkowski Kart. I’ve been working on an experimental, open-source kart racer built on SuperTuxKart, and I’d appreciate thoughtful feedback from people interested in either racing games or physics visualisation.

The game lets you adjust the effective speed of light and displays beta, gamma, and proper-time telemetry. It uses retarded positions, aberration, a conditional Doppler colour effect, and lensing effects. There are also arcade interpretations of the theme: a local time-dilation field, homing black holes, and linked wormholes that teleport karts. You can play with keyboard or gamepad, in split-screen or over LAN.

I don’t want to oversell the science. The vehicle model remains speed-capped arcade physics, not full relativistic dynamics, and there is no general-relativity solver or true portal view. Working experimental builds are supported for Windows, macOS, Linux and Android, and I have confirmed all four run. The exact Windows archive passed integrity/content checks and a 44.64-second headless race-init smoke, but is unsigned, lacks root `COPYING` and was not graphically retested. macOS has headless CI, Android structural CI, and Linux a possible `stk-assets` packaging concern. There is no project-run public server.

Project overview: <https://rchristie95.github.io/MinkowskiKart/>

Source: <https://github.com/rchristie95/MinkowskiKart>

Current release and package notes: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>

Issues: <https://github.com/rchristie95/MinkowskiKart/issues>

My main question is: which effect would you want explained most clearly in-game, and what kind of visual cue would help without getting in the way of racing?

Before posting, read and follow the target community’s self-promotion rules, participate in the discussion, and do not repost the same draft across multiple subreddits.

## Hacker News — cautious Show HN draft

**Publication gate (not part of the post):** The four supported builds are owner-confirmed running. Before posting, ensure the public download links are current, publish checksums, and repeat a direct-download smoke test on at least one named platform.

### Show HN: Minkowski Kart – an experimental relativistic kart-racing game

I built Minkowski Kart, an open-source experiment that turns selected ideas from relativity into kart-racing visuals, telemetry, and items. It is based on SuperTuxKart and licensed under GPLv3-or-later.

The game has an adjustable effective speed of light, beta/gamma and proper-time telemetry, retarded-position and aberration effects, and a conditional Doppler colour effect. Its item system includes a radius-limited time-dilation field, homing black holes with lensing, and linked wormholes that teleport karts.

Working experimental builds are supported for Windows, macOS, Linux and Android. I have run all four. Windows CI covers race entry, and the exact release ZIP passed integrity/content plus a 44.64-second headless race-init smoke; it is unsigned, lacks root `COPYING` and was not graphically retested. macOS CI is headless, Android CI is structural, Linux has an archive asset concern, and iOS remains developer/ad-hoc.

The engineering boundary is intentional: motion still uses speed-capped arcade physics, not full special-relativistic dynamics. It does not solve GR equations, implement full Lorentz contraction, or render a true view through each wormhole. OpenRelativity’s MIT-licensed colour-shift and mathematics work is credited as an inspiration.

Project and download notes: <https://rchristie95.github.io/MinkowskiKart/>

Source: <https://github.com/rchristie95/MinkowskiKart>

Experimental release: <https://github.com/rchristie95/MinkowskiKart/releases/tag/version_1.9>

Issues: <https://github.com/rchristie95/MinkowskiKart/issues>

I’d be interested in feedback on the rendering approach, how clearly the approximations are labelled, and the build experience.
