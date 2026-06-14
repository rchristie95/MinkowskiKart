# Spontaneous Breakdown

Game-ready MinkowskiKart/SuperTuxKart-style arena package.

- Runtime track folder: `stk-assets/tracks/spontaneous_breakdown`
- Select **Spontaneous Breakdown** as a battle arena.
- Scripted hazards are handled in `ThreeStrikesBattle` for track id `spontaneous_breakdown`.
- Art-source Blender scene remains in `SpontaneousBreakdown/`.
- Heavy full-map GLB/FBX exports were deleted locally to save disk.

Unity/Unreal import:
- Regenerate GLB/FBX from `SpontaneousBreakdown/Spontaneous_Breakdown.blend` if engine import is needed.
- Use `manifest.json` in this folder for collision intent and gameplay roles.
- The Goldstone hazard should be constrained to the valley radius; the donkey hazard should respawn at center, wait, roll outward, then play the bray cue on impact.
