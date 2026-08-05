# Contributing to Minkowski Kart

Thanks for helping improve Minkowski Kart. Contributions are welcome in code, physics documentation, testing, accessibility, translations, build engineering, and genuinely captured game media.

## Before opening a change

1. Search [existing issues](https://github.com/rchristie95/MinkowskiKart/issues) for related work.
2. For a bug or build failure, use the matching issue form and include your exact release or commit, operating system, graphics hardware, and `stdout.log` when available.
3. For a substantial gameplay, networking, physics, or asset change, open an issue first so design and licence questions can be resolved before implementation.

## Build and test

The repository uses CMake, with platform recipes under `.github/workflows/` and the platform-specific directories. Start with the source-build section in [README.md](README.md).

Before a pull request:

- build the target you changed;
- run the closest automated or manual smoke test available;
- describe what you could not test;
- do not commit build outputs, downloaded toolchains, credentials, signing material, or private configuration;
- preserve all upstream notices and per-asset licence files.

Physics-facing changes should state whether they alter gameplay dynamics, telemetry, rendering, or only presentation. Cite the equation or implementation being changed and add focused tests when practical. Do not describe an approximation as a complete physical simulation.

## Media and assets

Only submit media you created or have permission to redistribute. Gameplay screenshots must be genuine captures from the game, not mockups or generated substitutes. Record the creator, source, and licence for new marketing assets. Do not overwrite third-party attribution files.

## Pull requests

Keep changes focused and use the pull-request template. The inherited SuperTuxKart contribution agreement in the template requires contributors to agree to GPLv3-or-later and MPLv2-or-later licensing for their code contributions. Asset licences may differ and must be documented separately.

Use **Minkowski Kart** with a space in human-facing text. `MinkowskiKart` remains appropriate for executable names, packages, identifiers, and repository paths.
