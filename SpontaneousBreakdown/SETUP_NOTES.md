# Spontaneous Breakdown Setup Notes

## MinkowskiKart Runtime

The playable arena package is installed at `stk-assets/tracks/spontaneous_breakdown`.

- Select **Spontaneous Breakdown** as a battle arena.
- Runtime assets are SPM/PNG/OGG files generated for the engine in that folder.
- `stk-assets/tracks/spontaneous_breakdown/manifest.json` lists the runtime SPMs, Meshy source GLBs, collision intent, credit cost, and scripted gameplay roles.
- The Goldstone and Buridan's Ass events are driven by the `ThreeStrikesBattle` hook for track id `spontaneous_breakdown`.

## Unity

The heavy GLB/FBX exports were deleted locally to save disk. Regenerate exports from `Spontaneous_Breakdown.blend` only if engine import is needed.

1. Use the cleaned runtime manifest at `stk-assets/tracks/spontaneous_breakdown/manifest.json` for active asset names and collision roles.
2. Set the Goldstone boson hazard to kinematic circular movement around the valley radius.
3. For the donkey hazard, spawn at the center peak, pause briefly, choose a random angle, roll downhill, and play the bray cue on impact.
4. Keep the four ramps as static exact/mesh colliders.

## Unreal

1. Regenerate an FBX from `Spontaneous_Breakdown.blend` if needed.
2. Use the cleaned runtime manifest for active asset names and collision roles.
3. Drive `goldstone_boson_hazard` with circular movement constrained to the valley ring.
4. Drive the donkey hazard with a center respawn, hesitation timer, random yaw selection, rolling impulse, and bray audio cue on hit.

## Gameplay Intent

The physics joke is built into the layout: the donkey begins at the unstable symmetric center and chooses a broken-symmetry downhill direction, while the Goldstone boson stays on the degenerate vacuum valley and orbits the ring.

## Texture Pass

The cleaned runtime package keeps only the donkey hazard, Goldstone hazard, and two ramp assets, using their Meshy base-color textures. The removed decorative Meshy clutter and stale full-map exports were deleted to reclaim disk space.
