# Minkowski Kart — AJP manuscript

LaTeX source for a manuscript prepared for submission to the *American Journal of
Physics* (AJP), modelled on Sherin, Cheu, Tan & Kortemeyer, *"Visualizing
relativity: The OpenRelativity project,"* Am. J. Phys. **84**, 369 (2016).

The paper describes the Minkowski Kart game and the three substantive extensions
it makes over [OpenRelativity](https://github.com/MITGameLab/OpenRelativity):

1. **Per-object retarded-time + aberration optics** (Terrell–Penrose), instead of
   an instantaneous Lorentz contraction of static world geometry. See
   `src/relativity/relativity_math.cpp` and
   `data/shaders/utils/relativity_visual.vert`.
2. **Dynamic GPU mesh tessellation** that adaptively subdivides arbitrary track
   geometry so the nonlinear warp is resolved without faceting/clipping, with
   subdivision density driven by an aberration-magnification estimate. See
   `data/shaders/sp_tess.tesc` / `sp_tess.tese`.
3. **Playable relativistic dynamics** — adjustable `c`, `1/γ³` longitudinal mass,
   proper-time accumulation, velocity clamp, and physics-themed power-ups.

## Files

- `minkowski-kart.tex` — the manuscript (REVTeX 4.2, AIP/AJP options).
- `figures/` — drop screenshots here (the tessellation figure is drawn inline
  with TikZ and needs no external image).

## Build

Requires a TeX distribution with REVTeX 4.2 (TeX Live / MiKTeX both ship it).

```sh
pdflatex minkowski-kart
pdflatex minkowski-kart   # second pass resolves cross-references
```

Or with `latexmk`:

```sh
latexmk -pdf minkowski-kart.tex
```

The bibliography is embedded as a `thebibliography` block, so no BibTeX run is
required.

## Before submitting

- Add co-authors and institutional affiliation in the `\author`/`\affiliation`
  block.
- Replace the inline TikZ schematic and/or add captioned in-game screenshots in
  `figures/` (aberration of an overtaking kart, Doppler periphery + scanner
  window, tessellated vs. coarse track surface).
- Confirm the OpenRelativity color-shift reuse is cited to your satisfaction
  (it is reused with attribution under the MIT License).
