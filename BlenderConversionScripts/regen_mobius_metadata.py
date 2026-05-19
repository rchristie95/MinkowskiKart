#!/usr/bin/env python3
"""Regenerate Mobius track XML metadata without a full Blender mesh export."""

from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import MagicMock

sys.modules["bpy"] = MagicMock()

project_root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(project_root / "BlenderConversionScripts"))

import generate_mobius_track as gen  # noqa: E402

track_dir = project_root / "stk-assets" / "tracks" / "mobius_track"
track_dir.mkdir(parents=True, exist_ok=True)
gen.write_materials_xml(track_dir)
gen.write_track_xml(track_dir)
gen.write_scene_xml(track_dir)
gen.write_quads_xml(track_dir)
gen.write_graph_xml(track_dir)
gen.create_textures(track_dir)
for mesh in (
    gen.make_black_hole_halo_mesh(),
    gen.make_black_hole_accretion_mesh(),
    gen.make_black_hole_inner_glow_mesh(),
    gen.make_black_hole_photon_ring_mesh(),
    gen.make_black_hole_core_mesh(),
):
    out = track_dir / mesh["texture"].replace(".png", ".spm")
    gen.write_spm(out, mesh)
    print("Wrote", out.name)
zipper_mesh = gen.make_zipper_mesh()
gen.write_spm(track_dir / "mobius_zippers.spm", zipper_mesh)
print("Wrote mobius_zippers.spm")
print("Wrote", track_dir / "materials.xml")
print("Wrote", track_dir / "scene.xml")
for spec in gen.PLANET_SPECS:
    p = gen.planet_scene_position(spec)
    print(f"  planet {spec['id']:8} progress={spec['progress']:.3f} -> {p}")
bh = gen.planet_scene_position(gen.BLACK_HOLE_SPEC)
print(f"  black_hole progress={gen.BLACK_HOLE_SPEC['progress']:.3f} -> {bh}")
