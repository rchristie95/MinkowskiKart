#!/usr/bin/env python3
"""Generate the SuperTuxKart Mobius Trip prototype.

This script is intended to run inside Blender through Blender MCP. It builds a
procedural Blender scene for inspection, writes deterministic track metadata,
creates small generated textures, and exports minimal static SPM meshes without
requiring the external STK Blender SPM exporter.
"""

from __future__ import annotations

import math
import os
import json
import struct
import zlib
import time
import urllib.parse
import zipfile
import shutil
from pathlib import Path

try:
    import bpy
except ImportError as exc:  # pragma: no cover - this generator is Blender-first.
    raise RuntimeError("generate_mobius_track.py must run inside Blender") from exc


RADIUS = 82.0
ROAD_HALF_WIDTH = 8.0
GRAPH_HALF_WIDTH = 5.6
VISUAL_U_SEGMENTS = 768
VISUAL_V_SEGMENTS = 10
COLLISION_U_SEGMENTS = VISUAL_U_SEGMENTS
COLLISION_V_SEGMENTS = VISUAL_V_SEGMENTS
SAFETY_SURFACE_OFFSET = -0.42
SEAM_BRIDGE_SURFACE_OFFSET = 0.045
SUN_RADIUS = 14.0
SUN_CORONA_INNER_RADIUS = 17.0
SUN_CORONA_OUTER_RADIUS = 23.0
SUN_PROMINENCE_RADIUS = 31.0
BLACK_HOLE_CORE_RADIUS = 8.0
BLACK_HOLE_INNER_GLOW_RADIUS = 9.4
BLACK_HOLE_PHOTON_RING_RADIUS = 10.5
BLACK_HOLE_PHOTON_RING_WIDTH = 2.2
BLACK_HOLE_ACCRETION_INNER_RADIUS = 11.5
BLACK_HOLE_ACCRETION_OUTER_RADIUS = 22.0
BLACK_HOLE_HALO_INNER_RADIUS = 23.0
BLACK_HOLE_HALO_OUTER_RADIUS = 34.0
STAR_SPHERE_RADIUS = 360.0
SPHERE_U_SEGMENTS = 32
SPHERE_V_SEGMENTS = 16
SUN_U_SEGMENTS = 48
SUN_V_SEGMENTS = 24
SUN_CORONA_SEGMENTS = 128
BLACK_HOLE_DISK_SEGMENTS = 160
BLACK_HOLE_DISK_RINGS = 8
BLACK_HOLE_HALO_RINGS = 6
BLACK_HOLE_BLENDERKIT_REFERENCE_ASSET_ID = "5dc596e9-158f-4191-8f33-8ca68768d330"
# Space zipper pads evenly on the first-lap surface only.
ZIPPER_COUNT = 10
ZIPPER_U_POSITIONS = tuple(
    (0.78 + i * (2.0 * math.pi / ZIPPER_COUNT)) % (2.0 * math.pi)
    for i in range(ZIPPER_COUNT)
)
ZIPPER_LENGTH = 7.2
ZIPPER_WIDTH = 4.2
ZIPPER_SURFACE_LIFT = 0.08
START_U = 0.55
START_GRID_ROWS = 4
START_GRID_COLS = 3
START_GRID_LATERALS = (-4.4, 0.0, 4.4)
START_GRID_U_OFFSET = 0.11
START_GRID_U_SPACING = 0.082
START_GRID_MIN_DISTANCE = 3.0
START_GRID_LIFT = 0.55
PLANET_TEXTURE_SIZE = 1024
PLANET_MAX_TRIANGLES = 65000
THUMBNAIL_SOURCE = Path(
    os.environ.get("MOBIUS_THUMBNAIL_SOURCE", r"C:\Users\robso\Downloads\mobius.png")
)
PLANET_ZIP_DIR = Path(
    os.environ.get("MOBIUS_PLANET_ZIP_DIR", Path.home() / "Downloads" / "Planets")
)
PLANET_ZIP_OVERRIDES = {
    "saturn": Path(os.environ.get(
        "MOBIUS_SATURN_ZIP",
        r"C:\Users\robso\Downloads\Meshy_AI_Saturn_with_Rings_0518221200_texture_obj.zip",
    )),
}

ORBIT_BODY_COUNT = 9


def orbit_progress(index: int) -> float:
    """Evenly space orbital bodies on progress in [0, 1)."""
    return index / ORBIT_BODY_COUNT


_PLANET_SPECS_BASE = (
    {
        "id": "mercury",
        "display": "Mercury",
        "zip_pattern": "Meshy_AI_The_planet_Mercury_P*",
        "scale": 2.0,
        "lift": 13.0,
    },
    {
        "id": "venus",
        "display": "Venus",
        "zip_pattern": "Meshy_AI_planet_venus_stylised*",
        "scale": 2.8,
        "lift": 14.0,
    },
    {
        "id": "earth",
        "display": "Earth",
        "zip_pattern": "Meshy_AI_Earth*",
        "scale": 3.0,
        "lift": 15.0,
    },
    {
        "id": "mars",
        "display": "Mars",
        "zip_pattern": "Meshy_AI_mars__*",
        "scale": 2.5,
        "lift": 14.0,
    },
    {
        "id": "jupiter",
        "display": "Jupiter",
        "zip_pattern": "Meshy_AI_Jupiter_s_Swirl*",
        "scale": 9.0,
        "lift": 21.0,
    },
    {
        "id": "saturn",
        "display": "Saturn",
        "zip_pattern": "Meshy_AI_Saturn_with_Rings*",
        "scale": 6.0,
        "lift": 22.0,
    },
    {
        "id": "uranus",
        "display": "Uranus",
        "zip_pattern": "Meshy_AI_Uranus_in_Blue_Silenc*",
        "scale": 6.0,
        "lift": 18.0,
    },
    {
        "id": "neptune",
        "display": "Neptune",
        "zip_pattern": "Meshy_AI_planet_neptune_stylised*",
        "scale": 6.0,
        "lift": 19.0,
    },
)
PLANET_SPECS = tuple(
    {**spec, "progress": orbit_progress(index)}
    for index, spec in enumerate(_PLANET_SPECS_BASE)
)
BLACK_HOLE_SPEC = {
    "id": "black_hole",
    "display": "Black Hole",
    "progress": orbit_progress(8),
    "scale": 0.5,
    "lift": 19.0,
}


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vlength(a):
    return math.sqrt(max(vdot(a, a), 0.0))


def vnorm(a, fallback=(0.0, 1.0, 0.0)):
    length = vlength(a)
    if length <= 1.0e-8:
        return fallback
    return (a[0] / length, a[1] / length, a[2] / length)


def fmt_vec(v):
    return f"{v[0]:.3f} {v[1]:.3f} {v[2]:.3f}"


def fmt_xyz_attrs(v):
    return f'x="{v[0]:.3f}" y="{v[1]:.3f}" z="{v[2]:.3f}"'


def mobius_point(u, v, radius=RADIUS):
    cu = math.cos(u)
    su = math.sin(u)
    ch = math.cos(u * 0.5)
    sh = math.sin(u * 0.5)
    x = (radius + v * ch) * cu
    y = v * sh
    z = (radius + v * ch) * su
    return (x, y, z)


def mobius_du(u, v, radius=RADIUS):
    cu = math.cos(u)
    su = math.sin(u)
    ch = math.cos(u * 0.5)
    sh = math.sin(u * 0.5)
    radial = radius + v * ch
    dr = -0.5 * v * sh
    return (
        dr * cu - radial * su,
        0.5 * v * ch,
        dr * su + radial * cu,
    )


def mobius_dv(u):
    cu = math.cos(u)
    su = math.sin(u)
    ch = math.cos(u * 0.5)
    sh = math.sin(u * 0.5)
    return (ch * cu, sh, ch * su)


def mobius_normal(u, v):
    return vnorm(vcross(mobius_du(u, v), mobius_dv(u)))


def tangent_at(u, v=0.0):
    return vnorm(mobius_du(u, v), (0.0, 0.0, 1.0))


def mesh_dict(name, texture, verts, normals, uvs, indices):
    return {
        "name": name,
        "texture": texture,
        "verts": verts,
        "normals": normals,
        "uvs": uvs,
        "indices": indices,
    }


def make_strip_mesh(name, texture, half_width, u_segments, v_segments,
                    u_start=0, u_end=None, double_sided=False):
    if u_end is None:
        u_end = u_segments
    verts = []
    normals = []
    uvs = []
    indices = []
    for i in range(u_start, u_end + 1):
        u = (2.0 * math.pi) * i / u_segments
        for j in range(v_segments + 1):
            t = j / v_segments
            v = -half_width + 2.0 * half_width * t
            verts.append(mobius_point(u, v))
            normals.append(mobius_normal(u, v))
            uvs.append((i / 16.0, t))
    row = v_segments + 1
    for i in range(u_end - u_start):
        for j in range(v_segments):
            a = i * row + j
            b = (i + 1) * row + j
            c = (i + 1) * row + j + 1
            d = i * row + j + 1
            indices.extend((a, b, c, a, c, d))
            if double_sided:
                indices.extend((c, b, a, d, c, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_welded_mobius_surface(name, texture, half_width, u_segments,
                               v_segments, normal_offset=0.0,
                               double_sided=False):
    verts = []
    normals = []
    uvs = []
    for i in range(u_segments):
        u = (2.0 * math.pi) * i / u_segments
        for j in range(v_segments + 1):
            t = j / v_segments
            v = -half_width + 2.0 * half_width * t
            n = mobius_normal(u, v)
            verts.append(vadd(mobius_point(u, v), vmul(n, normal_offset)))
            normals.append(n)
            uvs.append((i / 16.0, t))

    indices = []
    row = v_segments + 1
    for i in range(u_segments):
        next_i = (i + 1) % u_segments
        for j in range(v_segments):
            if i == u_segments - 1:
                a = i * row + j
                b = next_i * row + (v_segments - j)
                c = next_i * row + (v_segments - j - 1)
                d = i * row + j + 1
            else:
                a = i * row + j
                b = next_i * row + j
                c = next_i * row + j + 1
                d = i * row + j + 1
            indices.extend((a, b, c, a, c, d))
            if double_sided:
                indices.extend((c, b, a, d, c, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def verify_mobius_visual_collision_sync(visual, collision, epsilon=1.0e-5):
    """Fail generation if road visual and collision vertices drift apart."""
    row = VISUAL_V_SEGMENTS + 1
    max_delta = 0.0
    for i in range(COLLISION_U_SEGMENTS):
        for j in range(COLLISION_V_SEGMENTS + 1):
            dv = vlength(vsub(visual["verts"][i * row + j],
                              collision["verts"][i * row + j]))
            max_delta = max(max_delta, dv)

    seam_i = VISUAL_U_SEGMENTS
    for j in range(COLLISION_V_SEGMENTS + 1):
        dv = vlength(vsub(visual["verts"][seam_i * row + j],
                          collision["verts"][COLLISION_V_SEGMENTS - j]))
        max_delta = max(max_delta, dv)

    expected_visual_triangles = VISUAL_U_SEGMENTS * VISUAL_V_SEGMENTS * 2
    actual_visual_triangles = len(visual["indices"]) // 3
    if actual_visual_triangles != expected_visual_triangles:
        raise RuntimeError(
            f"mobius_visual.spm road should be single-sided: "
            f"{actual_visual_triangles} triangles, expected "
            f"{expected_visual_triangles}")
    if max_delta > epsilon:
        raise RuntimeError(
            f"Mobius visual/collision road mismatch: max delta {max_delta}")
    return max_delta


def make_rail_visual_mesh(name="Mobius_Rails_Visual", u_start=0, u_end=None):
    if u_end is None:
        u_end = VISUAL_U_SEGMENTS
    verts = []
    normals = []
    uvs = []
    indices = []
    for sign in (-1.0, 1.0):
        for normal_sign in (-1.0, 1.0):
            base = len(verts)
            for i in range(u_start, u_end + 1):
                u = (2.0 * math.pi) * i / VISUAL_U_SEGMENTS
                p = mobius_point(u, sign * ROAD_HALF_WIDTH)
                n = vmul(mobius_normal(u, sign * ROAD_HALF_WIDTH), normal_sign)
                side = vnorm(vmul(mobius_dv(u), sign), (sign, 0.0, 0.0))
                center = vadd(vadd(p, vmul(n, 0.72)), vmul(side, 0.38))
                corners = (
                    vadd(vadd(center, vmul(n, 0.48)), vmul(side, -0.22)),
                    vadd(vadd(center, vmul(n, 0.48)), vmul(side, 0.22)),
                    vadd(vadd(center, vmul(n, -0.22)), vmul(side, 0.22)),
                    vadd(vadd(center, vmul(n, -0.22)), vmul(side, -0.22)),
                )
                for corner in corners:
                    verts.append(corner)
                    normals.append(vnorm(vsub(corner, center), n))
                uvs.extend(((i / 12.0, 0.0), (i / 12.0, 1.0), (i / 12.0, 1.0), (i / 12.0, 0.0)))
            for i in range(u_end - u_start):
                a = base + i * 4
                b = base + (i + 1) * 4
                for k in range(4):
                    k2 = (k + 1) % 4
                    indices.extend((a + k, b + k, b + k2, a + k, b + k2, a + k2))
    return mesh_dict(name, "mobius_rail.png", verts, normals, uvs, indices)


def make_guardrail_visual_mesh(name="Mobius_Guardrails_Visual", u_start=0, u_end=None):
    if u_end is None:
        u_end = VISUAL_U_SEGMENTS
    verts = []
    normals = []
    uvs = []
    indices = []
    for sign in (-1.0, 1.0):
        for normal_sign in (-1.0, 1.0):
            base = len(verts)
            for i in range(u_start, u_end + 1):
                u = (2.0 * math.pi) * i / VISUAL_U_SEGMENTS
                edge = mobius_point(u, sign * ROAD_HALF_WIDTH)
                n = vmul(mobius_normal(u, sign * ROAD_HALF_WIDTH), normal_sign)
                side = vnorm(vmul(mobius_dv(u), sign), (sign, 0.0, 0.0))
                lower = vadd(vadd(edge, vmul(n, 0.28)), vmul(side, 0.58))
                upper = vadd(vadd(edge, vmul(n, 2.55)), vmul(side, 0.88))
                cap = vadd(vadd(edge, vmul(n, 2.85)), vmul(side, 0.48))
                verts.extend((lower, upper, cap))
                normals.extend((side, side, vnorm(vadd(side, n), side)))
                uvs.extend(((i / 10.0, 0.0), (i / 10.0, 0.78), (i / 10.0, 1.0)))
            for i in range(u_end - u_start):
                a = base + i * 3
                b = base + (i + 1) * 3
                quads = ((0, 1), (1, 2))
                for low, high in quads:
                    p0 = a + low
                    p1 = b + low
                    p2 = b + high
                    p3 = a + high
                    indices.extend((p0, p1, p2, p0, p2, p3))
                    indices.extend((p2, p1, p0, p3, p2, p0))
    return mesh_dict(name, "mobius_guardrail.png", verts, normals, uvs, indices)


def make_rail_collision_mesh():
    verts = []
    normals = []
    uvs = []
    indices = []
    lanes = tuple((sign, normal_sign)
                  for sign in (-1.0, 1.0)
                  for normal_sign in (-1.0, 1.0))
    for sign, normal_sign in lanes:
        for i in range(COLLISION_U_SEGMENTS):
            u = (2.0 * math.pi) * i / COLLISION_U_SEGMENTS
            p = mobius_point(u, sign * ROAD_HALF_WIDTH)
            n = vmul(mobius_normal(u, sign * ROAD_HALF_WIDTH), normal_sign)
            side = vnorm(vmul(mobius_dv(u), sign), (sign, 0.0, 0.0))
            bottom = vadd(p, vmul(n, 0.05))
            top = vadd(vadd(p, vmul(n, 1.55)), vmul(side, 0.45))
            verts.extend((bottom, top))
            normals.extend((side, side))
            uvs.extend(((i / 16.0, 0.0), (i / 16.0, 1.0)))

    lane_indices = {lane: index for index, lane in enumerate(lanes)}

    def rail_index(lane_index, i, k):
        return lane_index * COLLISION_U_SEGMENTS * 2 + i * 2 + k

    for lane_index, (sign, normal_sign) in enumerate(lanes):
        for i in range(COLLISION_U_SEGMENTS):
            a = rail_index(lane_index, i, 0)
            if i == COLLISION_U_SEGMENTS - 1:
                next_lane_index = lane_indices[(-sign, -normal_sign)]
                b = rail_index(next_lane_index, 0, 0)
            else:
                b = rail_index(lane_index, i + 1, 0)
            if sign > 0:
                indices.extend((a, b, b + 1, a, b + 1, a + 1))
                indices.extend((b + 1, b, a, a + 1, b + 1, a))
            else:
                indices.extend((a, b + 1, b, a, a + 1, b + 1))
                indices.extend((b, b + 1, a, b + 1, a + 1, a))
    return mesh_dict("Mobius_Rail_Collision", "mobius_wall_collision.png", verts, normals, uvs, indices)


def make_uv_sphere_mesh(name, texture, radius, u_segments, v_segments,
                        inward=False, double_sided=False):
    verts = []
    normals = []
    uvs = []
    indices = []
    for j in range(v_segments + 1):
        t = j / v_segments
        theta = math.pi * t
        y = radius * math.cos(theta)
        ring = radius * math.sin(theta)
        for i in range(u_segments + 1):
            s = i / u_segments
            phi = 2.0 * math.pi * s
            p = (ring * math.cos(phi), y, ring * math.sin(phi))
            n = vnorm(p, (0.0, 1.0, 0.0))
            if inward:
                n = vmul(n, -1.0)
            verts.append(p)
            normals.append(n)
            uvs.append((s, t))
    row = u_segments + 1
    for j in range(v_segments):
        for i in range(u_segments):
            a = j * row + i
            b = a + 1
            c = (j + 1) * row + i + 1
            d = (j + 1) * row + i
            if inward:
                indices.extend((a, c, b, a, d, c))
                if double_sided:
                    indices.extend((b, c, a, c, d, a))
            else:
                indices.extend((a, b, c, a, c, d))
                if double_sided:
                    indices.extend((c, b, a, d, c, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_sun_core_mesh():
    verts = []
    normals = []
    uvs = []
    indices = []
    for j in range(SUN_V_SEGMENTS + 1):
        t = j / SUN_V_SEGMENTS
        theta = math.pi * t
        for i in range(SUN_U_SEGMENTS + 1):
            s = i / SUN_U_SEGMENTS
            phi = 2.0 * math.pi * s
            base = (
                math.sin(theta) * math.cos(phi),
                math.cos(theta),
                math.sin(theta) * math.sin(phi),
            )
            granule = 0.48 * math.sin(phi * 9.0 + theta * 5.0)
            granule += 0.34 * math.sin(phi * 17.0 - theta * 11.0)
            granule += 0.16 * math.sin(phi * 31.0 + theta * 23.0)
            flare = 0.32 * math.sin(phi * 4.0 + theta * 19.0)
            flare += 0.18 * math.sin(phi * 7.0 - theta * 29.0)
            radius = SUN_RADIUS + granule + flare
            verts.append(vmul(base, radius))
            normals.append(vnorm(base))
            uvs.append((s, t))
    row = SUN_U_SEGMENTS + 1
    for j in range(SUN_V_SEGMENTS):
        for i in range(SUN_U_SEGMENTS):
            a = j * row + i
            b = a + 1
            c = (j + 1) * row + i + 1
            d = (j + 1) * row + i
            indices.extend((a, b, c, a, c, d))
    return mesh_dict(
        "Mobius_Sun_Core", "mobius_sun_core.png", verts, normals, uvs, indices)


def make_sun_corona_mesh(name, radius, texture, tilt=0.0, phase=0.0):
    verts = []
    normals = []
    uvs = []
    indices = []
    ribbon_width = radius * 0.11
    tilt_sin = math.sin(tilt)
    tilt_cos = math.cos(tilt)
    for band in range(3):
        base = len(verts)
        band_phase = phase + band * 2.09439510239
        band_radius = radius * (0.94 + 0.07 * band)
        for i in range(SUN_CORONA_SEGMENTS + 1):
            t = i / SUN_CORONA_SEGMENTS
            a = 2.0 * math.pi * t
            wobble = 1.0 + 0.035 * math.sin(a * 5.0 + band_phase)
            local_radius = band_radius * wobble
            y = ribbon_width * 0.55 * math.sin(a * 3.0 + band_phase)
            p = (
                local_radius * math.cos(a),
                y,
                local_radius * math.sin(a),
            )
            tilted = (
                p[0],
                p[1] * tilt_cos - p[2] * tilt_sin,
                p[1] * tilt_sin + p[2] * tilt_cos,
            )
            outward = vnorm(tilted, (1.0, 0.0, 0.0))
            up = (0.0, tilt_cos, tilt_sin)
            inner = vadd(tilted, vmul(outward, -ribbon_width * 0.5))
            outer = vadd(tilted, vmul(outward, ribbon_width * 0.5))
            verts.extend((inner, outer))
            normals.extend((up, up))
            uvs.extend(((t * 3.0, 0.0), (t * 3.0, 1.0)))
        for i in range(SUN_CORONA_SEGMENTS):
            a0 = base + i * 2
            a1 = base + (i + 1) * 2
            indices.extend((a0, a1, a1 + 1, a0, a1 + 1, a0 + 1))
            indices.extend((a1 + 1, a1, a0, a0 + 1, a1 + 1, a0))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_black_hole_core_mesh():
    return make_uv_sphere_mesh(
        "Mobius_Black_Hole_Core", "mobius_black_hole_core.png",
        BLACK_HOLE_CORE_RADIUS, 48, 24, False, False)


def make_black_hole_ring_mesh(name, inner_radius, outer_radius, texture,
                              tilt=0.0, phase=0.0, double_sided=True,
                              ring_count=BLACK_HOLE_DISK_RINGS, wobble=0.22):
    verts = []
    normals = []
    uvs = []
    indices = []
    tilt_sin = math.sin(tilt)
    tilt_cos = math.cos(tilt)
    for ring in range(ring_count + 1):
        blend = ring / ring_count
        radius = inner_radius + (outer_radius - inner_radius) * blend
        for i in range(BLACK_HOLE_DISK_SEGMENTS + 1):
            t = i / BLACK_HOLE_DISK_SEGMENTS
            a = 2.0 * math.pi * t
            ripple = 1.0 + wobble * 0.12 * math.sin(a * 7.0 + phase + blend * 4.1)
            ripple += wobble * 0.06 * math.sin(a * 13.0 - blend * 2.3 + phase * 1.7)
            local_radius = radius * ripple
            y = wobble * math.sin(a * 4.0 + phase) * (1.0 - blend * 0.42)
            p = (
                local_radius * math.cos(a),
                y,
                local_radius * math.sin(a),
            )
            tilted = (
                p[0],
                p[1] * tilt_cos - p[2] * tilt_sin,
                p[1] * tilt_sin + p[2] * tilt_cos,
            )
            verts.append(tilted)
            normals.append((0.0, tilt_cos, tilt_sin))
            uvs.append((t * 3.0, blend))
    row = BLACK_HOLE_DISK_SEGMENTS + 1
    for ring in range(ring_count):
        for i in range(BLACK_HOLE_DISK_SEGMENTS):
            a = ring * row + i
            b = a + 1
            c = (ring + 1) * row + i + 1
            d = (ring + 1) * row + i
            indices.extend((a, b, c, a, c, d))
            if double_sided:
                indices.extend((c, b, a, d, c, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_black_hole_inner_glow_mesh():
    return make_black_hole_ring_mesh(
        "Mobius_Black_Hole_Inner_Glow",
        BLACK_HOLE_INNER_GLOW_RADIUS,
        BLACK_HOLE_INNER_GLOW_RADIUS + 1.6,
        "mobius_black_hole_inner_glow.png",
        0.0,
        1.2,
        wobble=0.08,
    )


def make_black_hole_accretion_mesh():
    return make_black_hole_ring_mesh(
        "Mobius_Black_Hole_Accretion",
        BLACK_HOLE_ACCRETION_INNER_RADIUS,
        BLACK_HOLE_ACCRETION_OUTER_RADIUS,
        "mobius_black_hole_accretion.png",
        0.0,
        0.4,
        wobble=0.28,
    )


def make_black_hole_photon_ring_mesh():
    return make_black_hole_ring_mesh(
        "Mobius_Black_Hole_Photon_Ring",
        BLACK_HOLE_PHOTON_RING_RADIUS,
        BLACK_HOLE_PHOTON_RING_RADIUS + BLACK_HOLE_PHOTON_RING_WIDTH,
        "mobius_black_hole_photon_ring.png",
        0.0,
        2.1,
        wobble=0.04,
    )


def make_black_hole_halo_mesh():
    return make_black_hole_ring_mesh(
        "Mobius_Black_Hole_Halo",
        BLACK_HOLE_HALO_INNER_RADIUS,
        BLACK_HOLE_HALO_OUTER_RADIUS,
        "mobius_black_hole_halo.png",
        0.0,
        0.9,
        wobble=0.14,
        ring_count=BLACK_HOLE_HALO_RINGS,
    )


def make_zipper_mesh():
    verts = []
    normals = []
    uvs = []
    indices = []
    half_length_u = (ZIPPER_LENGTH * 0.5) / RADIUS
    half_width = ZIPPER_WIDTH * 0.5
    u_subdivisions = 6
    v_subdivisions = 4

    def append_zipper_patch(u_center):
        base = len(verts)
        for i in range(u_subdivisions + 1):
            s = i / u_subdivisions
            u = u_center - half_length_u + 2.0 * half_length_u * s
            for j in range(v_subdivisions + 1):
                t = j / v_subdivisions
                lateral = -half_width + 2.0 * half_width * t
                n = mobius_normal(u, lateral)
                
                # Single-sided visual patch
                verts.append(vadd(mobius_point(u, lateral), vmul(n, ZIPPER_SURFACE_LIFT)))     
                normals.append(n)
                uvs.append((s, t))
                
        row = v_subdivisions + 1
        for i in range(u_subdivisions):
            for j in range(v_subdivisions):
                a = base + i * row + j
                b = base + (i + 1) * row + j
                c = base + (i + 1) * row + j + 1
                d = base + i * row + j + 1
                indices.extend((a, b, c, a, c, d))

    for u_center in ZIPPER_U_POSITIONS:
        append_zipper_patch(u_center)

    return mesh_dict("Mobius_Zippers", "mobius_zipper.png", verts, normals, uvs, indices)


def make_mobius_patch_mesh(name, texture, half_width, u_center, u_span,
                           u_segments, v_segments, normal_offset=0.0,
                           double_sided=False):
    verts = []
    normals = []
    uvs = []
    indices = []
    for i in range(u_segments + 1):
        s = i / u_segments
        u = u_center - u_span * 0.5 + u_span * s
        for j in range(v_segments + 1):
            t = j / v_segments
            v = -half_width + 2.0 * half_width * t
            n = mobius_normal(u, v)
            verts.append(vadd(mobius_point(u, v), vmul(n, normal_offset)))
            normals.append(n)
            uvs.append((s, t))
    row = v_segments + 1
    for i in range(u_segments):
        for j in range(v_segments):
            a = i * row + j
            b = (i + 1) * row + j
            c = (i + 1) * row + j + 1
            d = i * row + j + 1
            indices.extend((a, b, c, a, c, d))
            if double_sided:
                indices.extend((c, b, a, d, c, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_seam_jump_ramp_mesh(name, texture, collision=False):
    verts = []
    normals = []
    uvs = []
    indices = []
    u_segments = 18
    v_segments = 10
    u_start = 2.0 * math.pi - 0.58
    u_span = 0.46
    ramp_height = 1.65 if collision else 1.58
    base_offset = 0.085 if collision else 0.12
    half_width = ROAD_HALF_WIDTH * (0.98 if collision else 0.94)

    for normal_sign in (-1.0, 1.0):
        base = len(verts)
        for i in range(u_segments + 1):
            s = i / u_segments
            u = u_start + u_span * s
            lift = base_offset + ramp_height * (s * s * (3.0 - 2.0 * s))
            for j in range(v_segments + 1):
                t = j / v_segments
                v = -half_width + 2.0 * half_width * t
                edge_fade = 1.0 - 0.18 * abs(t * 2.0 - 1.0)
                n = vmul(mobius_normal(u, v), normal_sign)
                p = vadd(mobius_point(u, v), vmul(n, lift * edge_fade))
                verts.append(p)
                normals.append(n)
                uvs.append((s, t))
        row = v_segments + 1
        for i in range(u_segments):
            for j in range(v_segments):
                a = base + i * row + j
                b = base + (i + 1) * row + j
                c = base + (i + 1) * row + j + 1
                d = base + i * row + j + 1
                if normal_sign > 0:
                    indices.extend((a, b, c, a, c, d))
                    indices.extend((c, b, a, d, c, a))
                else:
                    indices.extend((a, c, b, a, d, c))
                    indices.extend((b, c, a, c, d, a))
    return mesh_dict(name, texture, verts, normals, uvs, indices)


def make_start_gate_mesh():
    verts = []
    normals = []
    uvs = []
    indices = []
    t = tangent_at(START_U)
    n = mobius_normal(START_U, 0.0)
    side = vnorm(mobius_dv(START_U), (1.0, 0.0, 0.0))
    center = mobius_point(START_U, 0.0)

    def add_box(box_center, axis_a, axis_b, axis_c, half_a, half_b, half_c):
        axes = (
            (axis_a, half_a),
            (axis_b, half_b),
            (axis_c, half_c),
        )
        corners = []
        for sa in (-1.0, 1.0):
            for sb in (-1.0, 1.0):
                for sc in (-1.0, 1.0):
                    p = box_center
                    for sign, (axis, half) in zip((sa, sb, sc), axes):
                        p = vadd(p, vmul(axis, sign * half))
                    corners.append(p)
        corner_index = {
            (-1, -1, -1): 0, (-1, -1, 1): 1,
            (-1, 1, -1): 2, (-1, 1, 1): 3,
            (1, -1, -1): 4, (1, -1, 1): 5,
            (1, 1, -1): 6, (1, 1, 1): 7,
        }
        faces = (
            ((1, 0, 0), axis_a, ((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1))),
            ((-1, 0, 0), vmul(axis_a, -1.0), ((-1, -1, 1), (-1, 1, 1), (-1, 1, -1), (-1, -1, -1))),
            ((0, 1, 0), axis_b, ((-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1))),
            ((0, -1, 0), vmul(axis_b, -1.0), ((-1, -1, 1), (-1, -1, -1), (1, -1, -1), (1, -1, 1))),
            ((0, 0, 1), axis_c, ((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1))),
            ((0, 0, -1), vmul(axis_c, -1.0), ((-1, 1, -1), (1, 1, -1), (1, -1, -1), (-1, -1, -1))),
        )
        for _, normal, keys in faces:
            face_base = len(verts)
            for uv, key in zip(((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)), keys):
                verts.append(corners[corner_index[key]])
                normals.append(vnorm(normal))
                uvs.append(uv)
            indices.extend((face_base, face_base + 1, face_base + 2,
                            face_base, face_base + 2, face_base + 3))

    ring_center = center
    ring_radius = ROAD_HALF_WIDTH
    tube_radius = 0.62
    major_segments = 64
    tube_segments = 8
    for i in range(major_segments):
        a = 2.0 * math.pi * i / major_segments
        radial = vnorm(vadd(vmul(side, math.cos(a)), vmul(n, math.sin(a))))
        tube_center = vadd(ring_center, vmul(radial, ring_radius))
        for j in range(tube_segments):
            b = 2.0 * math.pi * j / tube_segments
            tube_dir = vnorm(vadd(vmul(radial, math.cos(b)), vmul(t, math.sin(b))))
            verts.append(vadd(tube_center, vmul(tube_dir, tube_radius)))
            normals.append(tube_dir)
            uvs.append((i / major_segments, j / tube_segments))
    for i in range(major_segments):
        ni = (i + 1) % major_segments
        for j in range(tube_segments):
            nj = (j + 1) % tube_segments
            a = i * tube_segments + j
            b = ni * tube_segments + j
            c = ni * tube_segments + nj
            d = i * tube_segments + nj
            indices.extend((a, b, c, a, c, d))

    chevron_radius = ring_radius + 0.85
    for k in range(9):
        a = 2.0 * math.pi * (k / 9.0 + 0.25)
        radial = vnorm(vadd(vmul(side, math.cos(a)), vmul(n, math.sin(a))))
        tangent_ring = vnorm(vadd(vmul(side, -math.sin(a)), vmul(n, math.cos(a))))
        chevron_center = vadd(ring_center, vmul(radial, chevron_radius))
        add_box(chevron_center, tangent_ring, t, radial, 0.85, 0.26, 0.42)

    return mesh_dict("Mobius_Start_Gate", "mobius_start_gate.png", verts, normals, uvs, indices)


def make_star_sphere_mesh():
    return make_uv_sphere_mesh(
        "Mobius_Relativistic_Star_Sphere", "mobius_starfield.png",
        STAR_SPHERE_RADIUS, SPHERE_U_SEGMENTS, SPHERE_V_SEGMENTS, True, False)


def make_reset_surface():
    half = 150.0
    y = -62.0
    verts = [(-half, y, -half), (half, y, -half), (half, y, half), (-half, y, half)]
    normals = [(0.0, 1.0, 0.0)] * 4
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    indices = [0, 1, 2, 0, 2, 3]
    return mesh_dict("Reset_Fall_Surface", "reset_surface.png", verts, normals, uvs, indices)


def pack_normal_1010102(n):
    packed = 0
    for shift, value in ((0, n[0]), (10, n[1]), (20, n[2])):
        value = max(-1.0, min(1.0, value))
        if value > 0.0:
            part = int(value * 511.0 + 0.5)
        else:
            part = int(value * 512.0 - 0.5)
        packed |= (part & 1023) << shift
    return packed


def write_spm(path, mesh):
    verts = mesh["verts"]
    normals = [vnorm(n) for n in mesh["normals"]]
    uvs = mesh["uvs"]
    indices = mesh["indices"]
    if len(verts) > 65535:
        raise ValueError(f"{mesh['name']} has too many vertices for static SPM: {len(verts)}")

    mins = [min(v[i] for v in verts) for i in range(3)]
    maxs = [max(v[i] for v in verts) for i in range(3)]
    texture = mesh["texture"].encode("utf-8")

    with open(path, "wb") as f:
        f.write(b"SP")
        f.write(struct.pack("<B", 0x0A))  # SPMN static mesh version.
        f.write(struct.pack("<B", 0x01))  # normals present, no vertex colors/tangents.
        f.write(struct.pack("<6f", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]))
        f.write(struct.pack("<H", 1))
        f.write(struct.pack("<B", len(texture)))
        f.write(texture)
        f.write(struct.pack("<B", 0))
        f.write(struct.pack("<H", 1))  # one object/group
        f.write(struct.pack("<H", 1))  # one material mesh
        f.write(struct.pack("<I", len(verts)))
        f.write(struct.pack("<I", len(indices)))
        f.write(struct.pack("<H", 0))
        for pos, normal, uv in zip(verts, normals, uvs):
            f.write(struct.pack("<3f", pos[0], pos[1], pos[2]))
            f.write(struct.pack("<I", pack_normal_1010102(normal)))
            f.write(struct.pack("<e", float(uv[0])))
            f.write(struct.pack("<e", float(uv[1])))
        if len(verts) > 255:
            for index in indices:
                f.write(struct.pack("<H", index))
        else:
            for index in indices:
                f.write(struct.pack("<B", index))


def make_blender_material(name, color, blend=False):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    if blend:
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Alpha"].default_value = color[3]
        mat.blend_method = "BLEND"
        mat.use_screen_refraction = True
    return mat


def add_blender_object(mesh, material, color):
    blender_mesh = bpy.data.meshes.new(mesh["name"] + "_Mesh")
    faces = [tuple(mesh["indices"][i:i + 3]) for i in range(0, len(mesh["indices"]), 3)]
    blender_mesh.from_pydata(mesh["verts"], [], faces)
    blender_mesh.update(calc_edges=True)
    obj = bpy.data.objects.new(mesh["name"], blender_mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(material)
    obj["stk_texture"] = mesh["texture"]
    obj["spm_vertices"] = len(mesh["verts"])
    obj["spm_triangles"] = len(mesh["indices"]) // 3
    uv_layer = obj.data.uv_layers.new(name="UVMap")
    for poly in obj.data.polygons:
        poly.use_smooth = True
        for loop_index in poly.loop_indices:
            vertex_index = obj.data.loops[loop_index].vertex_index
            uv_layer.data[loop_index].uv = mesh["uvs"][vertex_index]
    return obj


def write_texture_png_file(path, width, height, pixel_fn):
    def png_chunk(tag, data):
        chunk = tag + data
        return (
            struct.pack(">I", len(data))
            + chunk
            + struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF)
        )

    rows = []
    for y in range(height):
        v = y / max(height - 1, 1)
        row = bytearray([0])
        for x in range(width):
            u = x / max(width - 1, 1)
            r, g, b, a = pixel_fn(u, v)
            row.extend((
                int(max(0, min(255, r * 255))),
                int(max(0, min(255, g * 255))),
                int(max(0, min(255, b * 255))),
                int(max(0, min(255, a * 255))),
            ))
        rows.append(bytes(row))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(png_chunk(b"IHDR", ihdr))
    png.extend(png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
    png.extend(png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_texture(path, width, height, pixel_fn, file_format="PNG"):
    if file_format == "PNG":
        write_texture_png_file(path, width, height, pixel_fn)
        return
    img = bpy.data.images.new(path.stem, width=width, height=height, alpha=True)
    pixels = []
    for y in range(height):
        v = y / max(height - 1, 1)
        for x in range(width):
            u = x / max(width - 1, 1)
            pixels.extend(pixel_fn(u, v))
    img.pixels.foreach_set(pixels)
    img.filepath_raw = str(path)
    img.file_format = file_format
    img.save()
    bpy.data.images.remove(img)


def install_scaled_image(source_path, destination_path, width, height, file_format):
    if not source_path.exists():
        return False
    img = bpy.data.images.load(str(source_path))
    try:
        img.scale(width, height)
        img.filepath_raw = str(destination_path)
        img.file_format = file_format
        img.save()
    finally:
        bpy.data.images.remove(img)
    return True


def write_solid_texture(path, color, width=PLANET_TEXTURE_SIZE, height=PLANET_TEXTURE_SIZE):
    img = bpy.data.images.new(path.stem, width=width, height=height, alpha=True)
    img.pixels.foreach_set(list(color) * width * height)
    img.filepath_raw = str(path)
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def save_image_texture(image, destination_path):
    if not image:
        return False
    copy = image.copy()
    try:
        copy.scale(PLANET_TEXTURE_SIZE, PLANET_TEXTURE_SIZE)
        copy.filepath_raw = str(destination_path)
        copy.file_format = "PNG"
        copy.save()
    finally:
        bpy.data.images.remove(copy)
    return destination_path.exists()


def find_object_texture_image(mesh_objects):
    best = None
    best_area = -1
    for obj in mesh_objects:
        for material in obj.data.materials:
            if material is None or material.node_tree is None:
                continue
            for node in material.node_tree.nodes:
                if node.bl_idname != "ShaderNodeTexImage" or not node.image:
                    continue
                width, height = node.image.size
                area = int(width) * int(height)
                if area > best_area:
                    best = node.image
                    best_area = area
    return best


def average_material_color(mesh_objects):
    colors = []
    for obj in mesh_objects:
        for material in obj.data.materials:
            if material is None:
                continue
            color = tuple(material.diffuse_color)
            if material.use_nodes and material.node_tree:
                node = material.node_tree.nodes.get("Principled BSDF")
                if node and "Base Color" in node.inputs:
                    color = tuple(node.inputs["Base Color"].default_value)
            colors.append(color)
    if not colors:
        return (0.55, 0.58, 0.62, 1.0)
    count = float(len(colors))
    return tuple(sum(c[i] for c in colors) / count for i in range(4))


def ensure_bake_uv(obj):
    uv_name = "BakeUV"
    if uv_name in obj.data.uv_layers:
        obj.data.uv_layers.active = obj.data.uv_layers[uv_name]
        return
    
    new_uv = obj.data.uv_layers.new(name=uv_name)
    obj.data.uv_layers.active = new_uv
    
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object else None
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.01)
    bpy.ops.object.mode_set(mode="OBJECT")


def bake_planet_texture(obj, destination_path):
    ensure_bake_uv(obj)
    image = bpy.data.images.new(
        destination_path.stem + "_bake",
        width=PLANET_TEXTURE_SIZE,
        height=PLANET_TEXTURE_SIZE,
        alpha=True,
    )
    image.generated_color = (0.0, 0.0, 0.0, 1.0)

    added_nodes = []
    for index, material in enumerate(obj.data.materials):
        if material is None:
            material = bpy.data.materials.new(f"{obj.name}_Material_{index}")
            obj.data.materials[index] = material
        material.use_nodes = True
        node_tree = material.node_tree
        texture_node = node_tree.nodes.new("ShaderNodeTexImage")
        texture_node.image = image
        texture_node.select = True
        node_tree.nodes.active = texture_node
        added_nodes.append((node_tree, texture_node))

    if not obj.data.materials:
        material = bpy.data.materials.new(f"{obj.name}_Material")
        obj.data.materials.append(material)
        material.use_nodes = True
        texture_node = material.node_tree.nodes.new("ShaderNodeTexImage")
        texture_node.image = image
        texture_node.select = True
        material.node_tree.nodes.active = texture_node
        added_nodes.append((material.node_tree, texture_node))

    scene = bpy.context.scene
    old_engine = scene.render.engine
    old_selected = list(bpy.context.selected_objects)
    old_active = bpy.context.view_layer.objects.active
    old_samples = getattr(scene.cycles, "samples", None) if hasattr(scene, "cycles") else None
    old_device = getattr(scene.cycles, "device", None) if hasattr(scene, "cycles") else None

    try:
        import addon_utils
        addon_utils.enable("cycles")
        try:
            bpy.ops.preferences.addon_enable(module="cycles")
        except Exception:
            pass
        scene.render.engine = "CYCLES"
        scene.cycles.samples = 32
        scene.cycles.device = "CPU"
    except Exception as e:
        raise RuntimeError(f"Cycles render engine is unavailable, so Blender cannot bake planet textures. ({e})")
    
    try:
        bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object else None
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.bake(
            type="DIFFUSE",
            pass_filter={"COLOR"},
            margin=12,
            use_clear=True,
        )
        image.filepath_raw = str(destination_path)
        image.file_format = "PNG"
        image.save()
    finally:
        for node_tree, texture_node in added_nodes:
            if texture_node.name in node_tree.nodes:
                node_tree.nodes.remove(texture_node)
        if old_samples is not None:
            scene.cycles.samples = old_samples
        if old_device is not None:
            scene.cycles.device = old_device
        scene.render.engine = old_engine
        bpy.ops.object.select_all(action="DESELECT")
        for selected in old_selected:
            if selected.name in bpy.data.objects:
                selected.select_set(True)
        if old_active and old_active.name in bpy.data.objects:
            bpy.context.view_layer.objects.active = old_active
        bpy.data.images.remove(image)

    return destination_path.exists()


def collect_images_from_socket(socket, seen=None):
    if seen is None:
        seen = set()
    images = []
    for link in socket.links:
        node = link.from_node
        if node in seen:
            continue
        seen.add(node)
        if node.bl_idname == "ShaderNodeTexImage" and node.image:
            images.append(node.image)
        for input_socket in getattr(node, "inputs", []):
            if input_socket.is_linked:
                images.extend(collect_images_from_socket(input_socket, seen))
    return images


def image_score(image, base_linked=False):
    name = (image.name + " " + Path(image.filepath or "").name).lower()
    score = 50 if base_linked else 0
    for token in ("albedo", "diffuse", "base", "color", "surface", "mercury", "venus", "earth", "mars", "jupiter", "uranus", "neptune"):
        if token in name:
            score += 20
    for token in ("normal", "bump", "rough", "metal", "mask", "alpha", "cloud", "night", "emission", "spec"):
        if token in name:
            score -= 40
    width, height = image.size
    score += min(int(width) * int(height), 4096 * 4096) / (4096 * 4096)
    return score


def material_base_color_image(material):
    if material is None or not material.use_nodes or material.node_tree is None:
        return None
    linked_images = []
    all_images = []
    for node in material.node_tree.nodes:
        if node.bl_idname == "ShaderNodeBsdfPrincipled" and "Base Color" in node.inputs:
            linked_images.extend(collect_images_from_socket(node.inputs["Base Color"]))
        if node.bl_idname == "ShaderNodeTexImage" and node.image:
            all_images.append(node.image)
    candidates = []
    candidates.extend((image_score(image, True), image) for image in linked_images)
    candidates.extend((image_score(image, False), image) for image in all_images)
    if not candidates:
        return None
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def material_base_color(material):
    if material is None:
        return (0.55, 0.58, 0.62, 1.0)
    color = tuple(material.diffuse_color)
    if material.use_nodes and material.node_tree:
        for node in material.node_tree.nodes:
            if node.bl_idname == "ShaderNodeBsdfPrincipled" and "Base Color" in node.inputs:
                color = tuple(node.inputs["Base Color"].default_value)
                break
    return color


def resized_image_pixels(image, width, height):
    copy = image.copy()
    try:
        copy.scale(width, height)
        pixels = [0.0] * (width * height * 4)
        copy.pixels.foreach_get(pixels)
        return pixels
    finally:
        bpy.data.images.remove(copy)


def generated_material_pixel(planet_id, material_name, base_color, u, v):
    name = material_name.lower()
    if planet_id == "saturn" and "ring" in name:
        stripe = 0.82 + 0.12 * math.sin(u * math.pi * 22.0)
        gap = 0.86 + 0.14 * math.sin(u * math.pi * 7.0 + 0.8)
        return (0.74 * stripe, 0.63 * stripe * gap, 0.45 * stripe, 0.88)
    if planet_id == "saturn":
        band = 0.72 + 0.16 * math.sin(v * math.pi * 18.0)
        warm = 0.08 * math.sin(v * math.pi * 5.0 + 0.4)
        return (0.74 * band + warm, 0.60 * band + warm * 0.5, 0.42 * band, 1.0)
    return (
        max(0.0, min(1.0, base_color[0])),
        max(0.0, min(1.0, base_color[1])),
        max(0.0, min(1.0, base_color[2])),
        max(0.0, min(1.0, base_color[3] if len(base_color) > 3 else 1.0)),
    )


def create_planet_atlas_texture(obj, destination_path, planet_id):
    ensure_bake_uv(obj)
    materials = list(obj.data.materials)
    if not materials:
        materials = [None]
    count = len(materials)
    cols = int(math.ceil(math.sqrt(count)))
    rows = int(math.ceil(count / cols))
    width = PLANET_TEXTURE_SIZE
    height = PLANET_TEXTURE_SIZE
    tile_w = width // cols
    tile_h = height // rows
    atlas_pixels = [0.0, 0.0, 0.0, 1.0] * (width * height)
    source_kinds = []

    for index, material in enumerate(materials):
        col = index % cols
        row = index // cols
        image = material_base_color_image(material)
        if image:
            tile_pixels = resized_image_pixels(image, tile_w, tile_h)
            source_kinds.append("image")
        else:
            material_name = material.name if material else ""
            base_color = material_base_color(material)
            tile_pixels = []
            for y in range(tile_h):
                v = y / max(tile_h - 1, 1)
                for x in range(tile_w):
                    u = x / max(tile_w - 1, 1)
                    tile_pixels.extend(generated_material_pixel(planet_id, material_name, base_color, u, v))
            source_kinds.append("generated-material" if planet_id == "saturn" else "material-color")

        for y in range(tile_h):
            dest_y = row * tile_h + y
            if dest_y >= height:
                continue
            for x in range(tile_w):
                dest_x = col * tile_w + x
                if dest_x >= width:
                    continue
                dest = (dest_y * width + dest_x) * 4
                src = (y * tile_w + x) * 4
                atlas_pixels[dest:dest + 4] = tile_pixels[src:src + 4]

    uv_layer = obj.data.uv_layers.active.data
    for polygon in obj.data.polygons:
        material_index = min(max(polygon.material_index, 0), count - 1)
        col = material_index % cols
        row = material_index // cols
        for loop_index in polygon.loop_indices:
            uv = uv_layer[loop_index].uv
            uv.x = (col + (uv.x % 1.0)) / cols
            uv.y = (row + (uv.y % 1.0)) / rows

    image = bpy.data.images.new(destination_path.stem, width=width, height=height, alpha=True)
    try:
        image.pixels.foreach_set(atlas_pixels)
        image.filepath_raw = str(destination_path)
        image.file_format = "PNG"
        image.save()
    finally:
        bpy.data.images.remove(image)
    if "image" in source_kinds and len(set(source_kinds)) == 1:
        return "atlas-image"
    return "atlas-" + "+".join(sorted(set(source_kinds)))


def sanitize_blenderkit_author(raw_result):
    allowed = {
        "aboutMe", "aboutMeUrl", "avatar128", "firstName", "fullName",
        "gravatarHash", "id", "lastName", "socialNetworks", "avatar256",
        "gravatarImg", "tooltip",
    }
    result = json.loads(json.dumps(raw_result))
    author = result.get("author")
    if isinstance(author, dict):
        result["author"] = {key: value for key, value in author.items() if key in allowed}
    return result


def blenderkit_modules():
    import importlib

    base = "bl_ext.user_default.blenderkit"
    return {
        "client": importlib.import_module(base + ".client_lib"),
        "download": importlib.import_module(base + ".download"),
        "paths": importlib.import_module(base + ".paths"),
        "search": importlib.import_module(base + ".search"),
        "timer": importlib.import_module(base + ".timer"),
        "utils": importlib.import_module(base + ".utils"),
    }


def configure_blenderkit_api_key():
    addon_name = "bl_ext.user_default.blenderkit"
    if addon_name not in bpy.context.preferences.addons:
        raise RuntimeError(
            "BlenderKit add-on is not enabled in Blender 5.1; cannot import required planet assets."
        )
    preferences = bpy.context.preferences.addons[addon_name].preferences
    api_key = os.environ.get("BLENDERKIT_API_KEY") or getattr(preferences, "api_key", "")
    if not api_key:
        raise RuntimeError(
            "BlenderKit API key is not configured. Set BLENDERKIT_API_KEY for this Blender session "
            "or authenticate BlenderKit locally; no procedural planet fallback will be generated."
        )
    preferences.api_key = api_key
    return api_key


def fetch_blenderkit_asset_data(asset_base_id, api_key, modules):
    query = urllib.parse.quote_plus(f"asset_base_id:{asset_base_id}")
    url = (
        f"{modules['paths'].BLENDERKIT_API}/search/?query={query}"
        "&page_size=1&dict_parameters=1"
    )
    response = modules["client"].blocking_request(
        url, "GET", modules["utils"].get_headers(api_key)
    )
    data = response.json()
    results = data.get("results") or []
    if not results:
        raise RuntimeError(f"BlenderKit asset {asset_base_id} was not found.")
    if results[0].get("canDownload") is False:
        name = results[0].get("displayName") or results[0].get("name") or asset_base_id
        raise RuntimeError(
            f"BlenderKit asset '{name}' is not downloadable with the current local account/plan."
        )
    return modules["search"].parse_result(sanitize_blenderkit_author(results[0]))


def wait_for_blenderkit_download(modules, timeout_seconds=180):
    deadline = time.monotonic() + timeout_seconds
    last_progress = -1
    while modules["download"].download_tasks:
        modules["timer"].client_communication_timer()
        progress = max(
            (task.get("progress", 0) for task in modules["download"].download_tasks.values()),
            default=100,
        )
        if progress != last_progress:
            print(f"BlenderKit planet import progress: {progress}%")
            last_progress = progress
        if time.monotonic() > deadline:
            modules["download"].cancel_running_downloads("Mobius planet import timeout")
            raise RuntimeError("Timed out while downloading/importing BlenderKit planet asset.")
        time.sleep(0.5)


def imported_mesh_objects(before_names):
    added_names = set(bpy.data.objects.keys()) - before_names
    added = [bpy.data.objects[name] for name in added_names]
    roots = []

    def collect_children(obj):
        children = []
        for child in obj.children:
            children.append(child)
            children.extend(collect_children(child))
        return children

    for obj in added:
        if obj.type == "MESH":
            roots.append(obj)
        for child in collect_children(obj):
            if child.type == "MESH" and child not in roots:
                roots.append(child)
    return [obj for obj in roots if obj.type == "MESH"]


def triangle_count_for_object(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def normalize_mesh_in_place(obj, planet_id):
    mesh = obj.data
    world_positions = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    if not world_positions:
        raise RuntimeError(f"BlenderKit planet {planet_id} imported no mesh vertices.")

    mins = [min(position[i] for position in world_positions) for i in range(3)]
    maxs = [max(position[i] for position in world_positions) for i in range(3)]
    center = tuple((mins[i] + maxs[i]) * 0.5 for i in range(3))
    extent = max(maxs[i] - mins[i] for i in range(3))
    if extent <= 1.0e-6:
        raise RuntimeError(f"BlenderKit planet {planet_id} has a degenerate bounding box.")

    scale = 2.0 / extent
    for vertex, world_position in zip(mesh.vertices, world_positions):
        normalized = vmul(vsub((world_position.x, world_position.y, world_position.z), center), scale)
        vertex.co = normalized
    obj.location = (0.0, 0.0, 0.0)
    obj.rotation_euler = (0.0, 0.0, 0.0)
    obj.scale = (1.0, 1.0, 1.0)
    mesh.update()


def blender_decimated_planet_object(planet_id, mesh_objects, triangle_budget):
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object else None
    bpy.ops.object.select_all(action="DESELECT")
    for obj in mesh_objects:
        obj.hide_set(False)
        obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_objects[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.convert(target="MESH")
    mesh_objects = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
    if not mesh_objects:
        raise RuntimeError(f"BlenderKit planet {planet_id} produced no mesh objects after conversion.")
    bpy.context.view_layer.objects.active = mesh_objects[0]
    if len(mesh_objects) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = f"Mobius_Planet_{planet_id.title()}_Source"
    obj.data.name = obj.name + "_Mesh"

    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    bpy.ops.object.mode_set(mode="OBJECT")

    for attempt in range(4):
        current_triangles = triangle_count_for_object(obj)
        if current_triangles <= triangle_budget:
            break
        modifier = obj.modifiers.new("Mobius planet Blender decimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = max(0.01, triangle_budget / current_triangles)
        if hasattr(modifier, "use_collapse_triangulate"):
            modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    current_triangles = triangle_count_for_object(obj)
    if current_triangles > triangle_budget:
        raise RuntimeError(
            f"BlenderKit planet {planet_id} could not be decimated below "
            f"{triangle_budget} triangles; final count is {current_triangles}."
        )

    triangulate = obj.modifiers.new("Mobius planet triangulate", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    normalize_mesh_in_place(obj, planet_id)
    return obj


def blender_to_stk_position(co):
    return (float(co.x), float(co.z), float(co.y))


def blender_to_stk_normal(normal):
    return vnorm((float(normal.x), float(normal.z), float(normal.y)))


def blender_object_to_mesh_dict(planet_id, obj, texture_name):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
    verts = []
    normals = []
    uvs = []
    indices = []
    vertex_map = {}
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None
        for triangle in mesh.loop_triangles:
            # Match the official STK Blender SPM exporter: converting Blender
            # Z-up coordinates to STK Y-up swaps handedness, so triangle
            # winding must be reversed at export.
            for loop_index in reversed(triangle.loops):
                vertex_index = mesh.loops[loop_index].vertex_index
                position = mesh.vertices[vertex_index].co
                normal = mesh.loops[loop_index].normal
                position_tuple = blender_to_stk_position(position)
                normal_tuple = blender_to_stk_normal(normal)
                if uv_layer:
                    uv = uv_layer[loop_index].uv
                    uv_tuple = (float(uv.x), 1.0 - float(uv.y))
                else:
                    n = vnorm(position_tuple)
                    uv_tuple = (
                        0.5 + math.atan2(n[2], n[0]) / (2.0 * math.pi),
                        0.5 - math.asin(max(-1.0, min(1.0, n[1]))) / math.pi,
                    )
                key = (
                    round(position_tuple[0], 6), round(position_tuple[1], 6), round(position_tuple[2], 6),
                    round(normal_tuple[0], 6), round(normal_tuple[1], 6), round(normal_tuple[2], 6),
                    round(uv_tuple[0], 6), round(uv_tuple[1], 6),
                )
                mapped = vertex_map.get(key)
                if mapped is None:
                    mapped = len(verts)
                    vertex_map[key] = mapped
                    verts.append(position_tuple)
                    normals.append(normal_tuple)
                    uvs.append(uv_tuple)
                indices.append(mapped)
    finally:
        bpy.data.meshes.remove(mesh)

    if not indices:
        raise RuntimeError(f"BlenderKit planet {planet_id} produced no exportable triangles.")
    if len(verts) > 65535:
        raise RuntimeError(
            f"BlenderKit planet {planet_id} remains too dense for this SPM writer: {len(verts)} vertices."
        )
    return mesh_dict(f"Mobius_Planet_{planet_id.title()}", texture_name, verts, normals, uvs, indices)


def planet_scene_position(spec):
    u = (START_U + spec["progress"] * 4.0 * math.pi) % (2.0 * math.pi)
    second_side = spec["progress"] >= 0.5
    lateral = 3.8 if int(spec["progress"] * 16.0) % 2 == 0 else -3.8
    point = mobius_point(u, lateral)
    normal = mobius_normal(u, lateral)
    if second_side:
        normal = vmul(normal, -1.0)
    side = vnorm(mobius_dv(u), (1.0, 0.0, 0.0))
    return vadd(vadd(point, vmul(normal, spec["lift"])), vmul(side, lateral * 0.7))


def import_local_planets(track_dir):
    planet_meshes = []
    manifest = {"source": "MeshyAI", "assets": []}
    temp_extract = track_dir / "temp_extract"
    
    for spec in PLANET_SPECS:
        if temp_extract.exists():
            shutil.rmtree(temp_extract)
        
        # Find the path (could be a zip file or a directory)
        override_path = PLANET_ZIP_OVERRIDES.get(spec["id"])
        if override_path is not None:
            if not override_path.exists():
                print(f"Warning: No override found for {spec['id']} at {override_path}")
                continue
            asset_path = override_path
        else:
            matches = list(PLANET_ZIP_DIR.glob(spec["zip_pattern"]))
            if not matches:
                 print(f"Warning: No match found for {spec['id']} with pattern {spec['zip_pattern']}")
                 continue
            asset_path = sorted(matches, key=lambda p: p.stat().st_mtime, reverse=True)[0]
        print(f"Importing {spec['id']} from {asset_path.name}...")
        
        search_dir = asset_path
        if asset_path.is_file() and asset_path.suffix.lower() == ".zip":
            temp_extract.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(asset_path, 'r') as zip_ref:
                zip_ref.extractall(temp_extract)
            search_dir = temp_extract
            
        objs = list(search_dir.rglob("*.obj"))
        if not objs:
            print(f"Warning: No OBJ found in {asset_path.name}")
            continue
            
        before = set(bpy.data.objects.keys())
        bpy.ops.wm.obj_import(filepath=str(objs[0]))
        mesh_objects = imported_mesh_objects(before)
        
        if not mesh_objects:
             print(f"Warning: Failed to import meshes for {spec['id']}")
             continue

        texture_name = f"mobius_planet_{spec['id']}.png"
        texture_path = track_dir / texture_name
        triangle_budget = spec.get("max_triangles", PLANET_MAX_TRIANGLES)
        
        bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object else None
        bpy.ops.object.select_all(action="DESELECT")
        for obj in mesh_objects:
            obj.hide_set(False)
            obj.select_set(True)
        bpy.context.view_layer.objects.active = mesh_objects[0]
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        if len(mesh_objects) > 1:
            bpy.ops.object.join()
        planet_object = bpy.context.view_layer.objects.active
        planet_object.name = f"Mobius_Planet_{spec['id'].title()}_Source"
        planet_object.data.name = planet_object.name + "_Mesh"
        
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.remove_doubles(threshold=0.0001)
        bpy.ops.object.mode_set(mode="OBJECT")
        
        normalize_mesh_in_place(planet_object, spec["id"])

        pngs = list(search_dir.rglob("*.png")) + list(search_dir.rglob("*.jpg"))
        if pngs:
            shutil.copy2(pngs[0], texture_path)
            texture_source = "original"
        else:
            print(f"Warning: No texture found for {spec['id']}, using fallback")
            write_solid_texture(texture_path, (0.5, 0.5, 0.5, 1.0))
            texture_source = "fallback"
        
        mesh = blender_object_to_mesh_dict(spec["id"], planet_object, texture_name)
        write_spm(track_dir / f"mobius_planet_{spec['id']}.spm", mesh)
        planet_meshes.append(mesh)
        
        manifest["assets"].append({
            "id": spec["id"],
            "displayName": spec["display"],
            "sourceFile": asset_path.name,
            "texture": texture_name,
            "textureSource": texture_source,
            "mesh": f"mobius_planet_{spec['id']}.spm",
            "vertices": len(mesh["verts"]),
            "triangles": len(mesh["indices"]) // 3,
            "triangleBudget": triangle_budget,
        })
        
    if temp_extract.exists():
        shutil.rmtree(temp_extract)
        
    (track_dir / "mobius_local_planets_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return planet_meshes


def record_blenderkit_black_hole_reference(track_dir):
    try:
        addon_name = "bl_ext.user_default.blenderkit"
        if addon_name not in bpy.context.preferences.addons:
            raise RuntimeError(
                "BlenderKit add-on is not enabled; procedural black hole reference metadata only."
            )
        preferences = bpy.context.preferences.addons[addon_name].preferences
        api_key = os.environ.get("BLENDERKIT_API_KEY") or getattr(preferences, "api_key", "")
        if not api_key:
            raise RuntimeError(
                "BlenderKit API key is not configured; procedural black hole reference metadata only."
            )
        modules = blenderkit_modules()
        asset_data = fetch_blenderkit_asset_data(
            BLACK_HOLE_BLENDERKIT_REFERENCE_ASSET_ID, api_key, modules)
        name = asset_data.get("displayName") or asset_data.get("name") or BLACK_HOLE_BLENDERKIT_REFERENCE_ASSET_ID
        print(f"BlenderKit black hole reference available: {name}")
    except Exception as exc:
        print(f"Warning: BlenderKit black hole reference unavailable: {exc}")


def create_textures(track_dir):
    def hash01(a, b, seed=0):
        value = math.sin(a * 127.1 + b * 311.7 + seed * 74.7) * 43758.5453
        return value - math.floor(value)

    def road(u, v):
        lane = 0.16 + 0.10 * math.sin(u * 40.0)
        edge = 0.35 if v < 0.08 or v > 0.92 else 0.0
        stripe = 0.36 if abs((v - 0.5)) < 0.018 else 0.0
        return (0.05 + edge + stripe, 0.06 + edge * 0.55 + lane, 0.09 + edge * 0.25 + lane, 1.0)

    def collision(u, v):
        return (0.08, 0.08, 0.08, 1.0)

    def wall(u, v):
        glow = 0.22 + 0.18 * math.sin(u * 55.0)
        return (0.04, 0.13 + glow, 0.20 + glow, 1.0)

    def rail(u, v):
        pulse = 0.45 + 0.35 * math.sin(u * 90.0)
        return (0.08, 0.50 + pulse * 0.3, 0.95, 0.78)

    def guardrail(u, v):
        seam = 0.22 if abs((u * 12.0) % 1.0 - 0.5) > 0.44 else 0.0
        band = 0.34 if 0.72 < v < 0.88 else 0.0
        base = 0.18 + seam
        return (base + band * 0.7, 0.24 + band, 0.30 + band * 0.55, 1.0)

    def gate(u, v):
        stripe = 0.32 if int(u * 12.0) % 2 == 0 else 0.0
        glow = 0.18 + 0.15 * math.sin(v * math.pi)
        return (0.12 + stripe, 0.66 + glow, 0.96, 1.0)

    def ramp(u, v):
        stripe = 0.22 if int(u * 8.0) % 2 == 0 else 0.0
        edge = 0.30 if v < 0.18 or v > 0.82 else 0.0
        return (0.10 + stripe + edge, 0.64 + edge * 0.5, 0.72 + edge * 0.2, 1.0)

    def arrow(u, v):
        return (0.88, 1.0, 0.28, 0.86)

    def reset(u, v):
        return (0.20, 0.02, 0.18, 0.35)

    def sun_core(u, v):
        granules = 0.28 * math.sin(u * 95.0 + math.sin(v * 31.0) * 4.0)
        granules += 0.18 * math.sin((u + v) * 177.0)
        granules += 0.10 * math.sin(u * 211.0 - v * 89.0)
        limb = 1.0 - abs(v - 0.5) * 0.8
        flare = 0.16 * math.sin(u * 13.0) * math.sin(v * math.pi)
        heat = max(0.0, min(1.0, 0.96 + granules + flare))
        return (1.0, 0.64 + 0.34 * heat, 0.18 + 0.28 * limb, 1.0)

    def sun_corona(u, v):
        strand = 0.45 + 0.35 * math.sin(u * 47.0 + v * 9.0)
        strand += 0.20 * math.sin(u * 113.0)
        strand += 0.12 * math.sin((u - v) * 181.0)
        edge = math.sin(math.pi * max(0.0, min(1.0, v)))
        alpha = max(0.0, min(0.98, edge * (0.48 + 0.42 * strand)))
        return (1.0, 0.72 + 0.22 * strand, 0.24 + 0.10 * strand, alpha)

    def black_hole_core(u, v):
        band = abs(v - 0.5) * 2.0
        equator = max(0.0, 1.0 - band * 1.05)
        swirl = 0.5 + 0.5 * math.sin(u * 31.0 + math.sin(v * 14.0) * 4.5)
        hotspot = math.exp(-((band - 0.44) ** 2) / 0.010) * swirl
        horizon = math.exp(-((band - 0.06) ** 2) / 0.0025)
        chroma = 0.5 + 0.5 * math.sin(u * math.pi * 4.0)
        warm_r = hotspot * 1.0
        warm_g = hotspot * (0.48 + 0.18 * chroma)
        warm_b = hotspot * (0.08 + 0.12 * (1.0 - chroma))
        cool_r = horizon * (0.22 + 0.25 * chroma)
        cool_g = horizon * 0.58
        cool_b = horizon * 1.0
        alpha = max(0.0, min(1.0, hotspot * 0.94 + horizon * 0.88))
        return (
            min(1.0, warm_r + cool_r + 0.015),
            min(1.0, warm_g + cool_g + 0.01),
            min(1.0, warm_b + cool_b + 0.02),
            alpha,
        )

    def black_hole_inner_glow(u, v):
        ring = math.sin(math.pi * max(0.0, min(1.0, v))) ** 1.4
        pulse = 0.62 + 0.38 * math.sin(u * 113.0 + v * 21.0)
        flicker = 0.85 + 0.15 * math.sin(u * 241.0)
        alpha = max(0.0, min(0.98, ring * pulse * flicker))
        return (1.0, 0.82 + 0.12 * pulse, 0.42 + 0.35 * flicker, alpha)

    def black_hole_accretion(u, v):
        spiral = math.sin(u * 34.0 - v * 16.0 + math.sin(u * 73.0) * 2.4)
        arm = 0.5 + 0.5 * spiral
        turbulence = 0.5 + 0.5 * math.sin(u * 127.0 + v * 33.0)
        inner = (1.0 - max(0.0, min(1.0, v))) ** 1.35
        edge = math.sin(math.pi * max(0.0, min(1.0, v))) ** 0.75
        doppler = 0.5 + 0.5 * math.sin(u * math.pi * 4.0)
        heat = max(0.0, min(1.0, inner * 1.15 + arm * 0.62 + turbulence * 0.18))
        return (
            1.0,
            0.18 + 0.62 * heat * (0.65 + 0.35 * doppler),
            0.04 + 0.48 * heat * (1.05 - doppler * 0.55),
            max(0.0, min(0.94, edge * (0.52 + 0.48 * heat) * (0.75 + 0.25 * arm))),
        )

    def black_hole_photon_ring(u, v):
        band = math.sin(math.pi * max(0.0, min(1.0, v))) ** 2.2
        pulse = 0.68 + 0.32 * math.sin(u * 97.0)
        shimmer = 0.9 + 0.1 * math.sin(u * 211.0 - v * 40.0)
        alpha = max(0.0, min(0.99, band * pulse * shimmer))
        return (
            0.75 + 0.25 * pulse,
            0.88 + 0.12 * shimmer,
            1.0,
            alpha,
        )

    def black_hole_halo(u, v):
        radial = max(0.0, min(1.0, v))
        falloff = (1.0 - radial) ** 1.8
        veil = 0.45 + 0.55 * math.sin(u * 19.0 + radial * 8.0)
        lens = 0.5 + 0.5 * math.sin(u * 47.0 - radial * 12.0)
        alpha = max(0.0, min(0.55, falloff * veil * (0.35 + 0.25 * lens)))
        return (
            0.35 + 0.45 * lens,
            0.22 + 0.38 * veil,
            0.72 + 0.28 * lens,
            alpha,
        )

    def zipper(u, v):
        edge = 0.42 if v < 0.14 or v > 0.86 else 0.0
        stripe = 0.22 if int(u * 10.0) % 2 == 0 else 0.0
        arrow_center = abs(v - 0.5)
        arrow_tip = max(0.0, 1.0 - abs(u - 0.76) / 0.20)
        arrow_tail = 1.0 if 0.18 < u < 0.58 and arrow_center < 0.18 else 0.0
        arrow_wing = max(0.0, 1.0 - abs(arrow_center - (0.78 - u) * 0.58) / 0.08) if 0.58 <= u <= 0.92 else 0.0
        arrow = max(arrow_tail, min(1.0, arrow_tip * arrow_wing))
        glow = max(edge, arrow)
        return (
            0.02 + 0.12 * stripe + 0.92 * glow,
            0.20 + 0.52 * stripe + 0.82 * glow,
            0.36 + 0.28 * stripe + 0.12 * arrow,
            1.0,
        )

    def starfield(u, v):
        milky_way = math.exp(-((v - 0.53 - 0.07 * math.sin(u * math.pi * 2.0)) ** 2) / 0.0025)
        color = [0.006 + 0.025 * milky_way, 0.008 + 0.035 * milky_way, 0.018 + 0.065 * milky_way]
        cells_x = 128
        cells_y = 64
        cx = int(u * cells_x)
        cy = int(v * cells_y)
        fx = u * cells_x - cx
        fy = v * cells_y - cy
        density = 0.018 + 0.050 * milky_way
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                sx = cx + ox
                sy = cy + oy
                chance = hash01(sx, sy, 1)
                if chance > density:
                    continue
                px = hash01(sx, sy, 2)
                py = hash01(sx, sy, 3)
                dx = fx - ox - px
                dy = fy - oy - py
                dist2 = dx * dx + dy * dy
                size = 0.002 + 0.010 * hash01(sx, sy, 4)
                star = math.exp(-dist2 / size)
                temp = hash01(sx, sy, 5)
                brightness = star * (0.45 + 1.9 * hash01(sx, sy, 6))
                color[0] += brightness * (0.75 + 0.35 * temp)
                color[1] += brightness * (0.82 + 0.18 * (1.0 - temp))
                color[2] += brightness * (1.05 - 0.35 * temp)
        return (min(color[0], 1.0), min(color[1], 1.0), min(color[2], 1.0), 1.0)

    def screenshot(u, v):
        dx = u - 0.5
        dy = v - 0.5
        r = math.sqrt(dx * dx + dy * dy)
        ring = math.exp(-((r - 0.34) ** 2) / 0.002)
        marker = math.exp(-((dy + 0.10) ** 2) / 0.006) * math.exp(-((abs(dx) - 0.18) ** 2) / 0.01)
        return (0.02 + ring * 0.22 + marker * 0.35,
                0.04 + ring * 0.62 + marker * 0.60,
                0.08 + ring * 0.90 + marker * 0.20,
                1.0)

    minimap_points = []
    for i in range(720):
        a = 2.0 * math.pi * i / 720.0
        minimap_points.append((0.86 * math.sin(a), 0.48 * math.sin(2.0 * a)))
    gate_x = 0.86 * math.sin(START_U)
    gate_y = 0.48 * math.sin(2.0 * START_U)
    gate_dx = 0.86 * math.cos(START_U)
    gate_dy = 0.96 * math.cos(2.0 * START_U)
    gate_len = max(math.sqrt(gate_dx * gate_dx + gate_dy * gate_dy), 1.0e-6)
    gate_nx = -gate_dy / gate_len
    gate_ny = gate_dx / gate_len

    def minimap(u, v):
        x = u * 2.0 - 1.0
        y = v * 2.0 - 1.0
        d2 = min((x - px) * (x - px) + (y - py) * (y - py)
                 for px, py in minimap_points)
        d = math.sqrt(d2)
        outer = max(0.0, min(1.0, (0.105 - d) / 0.025))
        inner = max(0.0, min(1.0, (0.060 - d) / 0.018))
        alpha = max(0.0, min(0.62, outer * 0.48 + inner * 0.22))
        blue = 0.08 * inner
        color = [0.58 + blue, 0.62 + blue, 0.68 + blue * 1.5, alpha]
        rx = x - gate_x
        ry = y - gate_y
        along = rx * gate_nx + ry * gate_ny
        across = rx * gate_dx / gate_len + ry * gate_dy / gate_len
        gate_line = max(0.0, min(1.0, (0.030 - abs(across)) / 0.012))
        gate_span = max(0.0, min(1.0, (0.155 - abs(along)) / 0.020))
        gate_dot = max(0.0, min(1.0, (0.075 - math.sqrt(rx * rx + ry * ry)) / 0.022))
        gate_mark = max(gate_line * gate_span, gate_dot * 0.7)
        if gate_mark > 0.0:
            color[0] = max(color[0], 1.0 * gate_mark)
            color[1] = max(color[1], 0.74 * gate_mark)
            color[2] = max(color[2], 0.18 * gate_mark)
            color[3] = max(color[3], min(0.95, 0.36 + gate_mark * 0.45))
        return tuple(color)

    write_texture(track_dir / "mobius_road_visual.png", 128, 128, road)
    write_texture(track_dir / "mobius_collision.png", 8, 8, collision)
    write_texture(track_dir / "mobius_safety_collision.png", 8, 8, collision)
    write_texture(track_dir / "mobius_wall_collision.png", 8, 8, wall)
    write_texture(track_dir / "mobius_rail.png", 128, 32, rail)
    write_texture(track_dir / "mobius_guardrail.png", 64, 32, guardrail)
    write_texture(track_dir / "mobius_start_gate.png", 64, 32, gate)
    write_texture(track_dir / "mobius_seam_ramp.png", 64, 32, ramp)
    write_texture(track_dir / "mobius_seam_ramp_visual.png", 64, 32, ramp)
    write_texture(track_dir / "mobius_sun_core.png", 512, 256, sun_core)
    write_texture(track_dir / "mobius_sun_corona.png", 256, 64, sun_corona)
    write_texture(track_dir / "mobius_black_hole_core.png", 256, 256, black_hole_core)
    write_texture(track_dir / "mobius_black_hole_inner_glow.png", 256, 64, black_hole_inner_glow)
    write_texture(track_dir / "mobius_black_hole_accretion.png", 512, 128, black_hole_accretion)
    write_texture(track_dir / "mobius_black_hole_photon_ring.png", 256, 64, black_hole_photon_ring)
    write_texture(track_dir / "mobius_black_hole_halo.png", 256, 128, black_hole_halo)
    write_texture(track_dir / "mobius_starfield.png", 512, 256, starfield)
    write_texture(track_dir / "mobius_zipper.png", 128, 64, zipper)
    write_texture(track_dir / "reset_surface.png", 8, 8, reset)
    write_texture(track_dir / "mobius_minimap.png", 512, 512, minimap)
    write_texture(track_dir / "screenshot.jpg", 512, 256, screenshot, file_format="JPEG")
    install_scaled_image(THUMBNAIL_SOURCE, track_dir / "screenshot.jpg", 512, 256, "JPEG")


def write_track_xml(track_dir):
    (track_dir / "track.xml").write_text(
        """<?xml version="1.0"?>
<track  name           = "Mobius Trip"
        version        = "7"
        groups         = "minkowski"
        designer       = "Robson Christie"
        screenshot     = "screenshot.jpg"
        music          = "highway_gravel.music"
        smooth-normals = "true"
        default-number-of-laps = "3"
        reverse        = "N"
        clouds         = "N"
        is-during-day  = "N"
        shadows        = "Y">
</track>
""",
        encoding="utf-8",
    )


def write_materials_xml(track_dir):
    planet_materials = "\n".join(
        f'  <material name="mobius_planet_{spec["id"]}.png" ignore="Y" backface-culling="N"/>'
        for spec in PLANET_SPECS
    )
    (track_dir / "materials.xml").write_text(
        f"""<?xml version="1.0"?>
<materials>
  <material name="mobius_road_visual.png" ignore="Y" backface-culling="N"/>
  <material name="mobius_collision.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>
  <material name="mobius_safety_collision.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>
  <material name="mobius_wall_collision.png" high-adhesion="Y" backface-culling="N"/>
  <material name="mobius_rail.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_guardrail.png" ignore="Y" backface-culling="N"/>
  <material name="mobius_start_gate.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_seam_ramp.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>
  <material name="mobius_seam_ramp_visual.png" ignore="Y" backface-culling="N"/>
  <material name="mobius_sun_core.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_sun_corona.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_black_hole_core.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_black_hole_inner_glow.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_black_hole_accretion.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_black_hole_photon_ring.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_black_hole_halo.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_starfield.png" shader="additive" ignore="Y" backface-culling="N"/>
  <material name="mobius_zipper.png" shader="alphablend" ignore="N" backface-culling="N">
    <zipper duration="2.5" max-speed-increase="12.0" fade-out-time="2.0" speed-gain="5.0" min-speed="0.0"/>
  </material>
  <material name="reset_surface.png" reset="Y" falling-effect="Y" backface-culling="N"/>
{planet_materials}
</materials>
""",
        encoding="utf-8",
    )


def quad_points(i):
    u0 = (2.0 * math.pi) * i / COLLISION_U_SEGMENTS
    u1 = (2.0 * math.pi) * (i + 1) / COLLISION_U_SEGMENTS
    return (
        mobius_point(u0, -GRAPH_HALF_WIDTH),
        mobius_point(u0, GRAPH_HALF_WIDTH),
        mobius_point(u1, GRAPH_HALF_WIDTH),
        mobius_point(u1, -GRAPH_HALF_WIDTH),
    )


def write_quads_xml(track_dir):
    lines = [
        '<?xml version="1.0"?>',
        '<quads>',
        '  <height-testing min="-24.000000" max="24.000000"/>',
        '  <!-- Driveline: two-tour Mobius sprint loop -->',
    ]
    for i in range(COLLISION_U_SEGMENTS * 2):
        p0, p1, p2, p3 = quad_points(i)
        lines.append(
            f'  <quad p0="{fmt_vec(p0)}" p1="{fmt_vec(p1)}" '
            f'p2="{fmt_vec(p2)}" p3="{fmt_vec(p3)}"/>'
        )
    lines.append("</quads>")
    (track_dir / "quads.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_graph_xml(track_dir):
    (track_dir / "graph.xml").write_text(
        f"""<?xml version="1.0"?>
<graph>
  <node-list from-quad="0" to-quad="{COLLISION_U_SEGMENTS * 2 - 1}"/>
  <edge-loop from="0" to="{COLLISION_U_SEGMENTS * 2 - 1}"/>
</graph>
""",
        encoding="utf-8",
    )


def check_line_at(u, half_width=GRAPH_HALF_WIDTH):
    return mobius_point(u, -half_width), mobius_point(u, half_width)


def item_position(u, lateral, lift=1.2):
    p = mobius_point(u, lateral)
    n = mobius_normal(u, lateral)
    return vadd(p, vmul(n, lift))


def surface_item_attrs(u, lateral):
    return (
        f'{fmt_xyz_attrs(mobius_point(u, lateral))} '
        f'surface-normal="{fmt_vec(mobius_normal(u, lateral))}" drop="false"'
    )


def start_position(row, col):
    u = START_U - START_GRID_U_OFFSET - row * START_GRID_U_SPACING
    lateral = START_GRID_LATERALS[col]
    p = item_position(u, lateral, START_GRID_LIFT)
    t = tangent_at(u)
    heading = math.degrees(math.atan2(t[0], t[2]))
    return p, heading


def generated_start_positions():
    starts = []
    for row in range(START_GRID_ROWS):
        for col in range(START_GRID_COLS):
            idx = row * START_GRID_COLS + col
            p, heading = start_position(row, col)
            starts.append((idx, p, heading))
    return starts


def start_grid_self_check():
    starts = generated_start_positions()
    min_distance = float("inf")
    closest_pair = None
    for i in range(len(starts)):
        for j in range(i + 1, len(starts)):
            distance = vlength(vsub(starts[i][1], starts[j][1]))
            if distance < min_distance:
                min_distance = distance
                closest_pair = (starts[i][0], starts[j][0])
    if min_distance < START_GRID_MIN_DISTANCE:
        raise RuntimeError(
            f"Mobius start grid overlap risk: starts {closest_pair} are only "
            f"{min_distance:.2f}m apart."
        )
    return {"count": len(starts), "min_pairwise_distance": min_distance, "closest_pair": closest_pair}


def write_scene_xml(track_dir):
    finish_1a, finish_1b = check_line_at(START_U)
    q1_1a, q1_1b = check_line_at((START_U + math.pi * 0.50))
    q2_1a, q2_1b = check_line_at((START_U + math.pi * 1.0))
    q3_1a, q3_1b = check_line_at((START_U + math.pi * 1.50))

    finish_2a, finish_2b = check_line_at((START_U + math.pi * 2.0))
    q1_2a, q1_2b = check_line_at((START_U + math.pi * 2.50))
    q2_2a, q2_2b = check_line_at((START_U + math.pi * 3.0))
    q3_2a, q3_2b = check_line_at((START_U + math.pi * 3.50))
    lines = [
        '<?xml version="1.0"?>',
        '<scene>',
        '  <sky-color rgb="0 0 0"/>',
        '  <camera far="900"/>',
        '  <sun xyz="0 0 0" sun-diffuse="255 255 238" sun-specular="255 255 245" ambient="54 48 40" fog="false"/>',
        '  <lightshaft opacity="1.35" color="255 225 150" xyz="0 0 0"/>',
        '  <track model="mobius_visual.spm" x="0" y="0" z="0">',
        '    <static-object model="mobius_star_sphere.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_seam_jump_ramp_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_seam_bridge_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_safety_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_rail_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="reset_fall_surface.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_rails_visual.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_guardrails_visual.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_seam_jump_ramp_visual.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_start_gate.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost" shadow-pass="false"/>',
    ]
    lines.extend([
        '    <static-object model="mobius_zippers.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost-texture" shadow-pass="false"/>',
        '  </track>',
        '  <object id="mobius_sun_core" type="animation" model="mobius_sun_core.spm" xyz="0 0 0" hpr="0 0 0" scale="1.18 1.18 1.18" interaction="ghost" glow="255 225 130" forcedbloom="true" bloompower="4.0" shadow-pass="false">',
        '    <curve channel="RotY" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="450.000 360.000"/>',
        '    </curve>',
        '  </object>',
        '  <object id="mobius_sun_corona_inner" type="animation" model="mobius_sun_corona_inner.spm" xyz="0 0 0" hpr="0 0 0" scale="1.24 1.24 1.24" interaction="ghost" glow="255 196 90" forcedbloom="true" bloompower="3.5" shadow-pass="false">',
        '    <curve channel="RotZ" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="300.000 360.000"/>',
        '    </curve>',
        '  </object>',
        '  <object id="mobius_sun_corona_outer" type="animation" model="mobius_sun_corona_outer.spm" xyz="0 0 0" hpr="18 0 0" scale="1.30 1.30 1.30" interaction="ghost" glow="255 184 70" forcedbloom="true" bloompower="3.0" shadow-pass="false">',
        '    <curve channel="RotY" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="525.000 -360.000"/>',
        '    </curve>',
        '  </object>',
        '  <object id="mobius_sun_prominence" type="animation" model="mobius_sun_prominence.spm" xyz="0 0 0" hpr="-12 0 0" scale="1.36 1.36 1.36" interaction="ghost" glow="255 160 55" forcedbloom="true" bloompower="2.6" shadow-pass="false">',
        '    <curve channel="RotZ" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="420.000 360.000"/>',
        '    </curve>',
        '  </object>',
        '  <light xyz="0 0 0" id="mobius_sun_light" distance="520.00" energy="13.00" color="255 238 170" type="point"/>',
    ])
    for spec in PLANET_SPECS:
        p = planet_scene_position(spec)
        scale = spec["scale"]
        lines.append(
            f'  <object id="mobius_planet_{spec["id"]}" type="animation" model="mobius_planet_{spec["id"]}.spm" {fmt_xyz_attrs(p)} '
            f'hpr="0 0 0" scale="{scale:.3f} {scale:.3f} {scale:.3f}" '
            f'interaction="ghost" shadow-pass="false" skeletal-animation="false"/>'
        )
    black_hole_position = planet_scene_position(BLACK_HOLE_SPEC)
    black_hole_scale = BLACK_HOLE_SPEC["scale"]
    black_hole_xyz = fmt_xyz_attrs(black_hole_position)
    black_hole_scale_attr = f'{black_hole_scale:.3f} {black_hole_scale:.3f} {black_hole_scale:.3f}'
    black_hole_layers = (
        ("mobius_planet_black_hole_halo", "mobius_black_hole_halo.spm", "0 0 0", "RotY", 480.0, 360.0),
        ("mobius_planet_black_hole_accretion", "mobius_black_hole_accretion.spm", "0 0 0", "RotY", 120.0, 360.0),
        ("mobius_planet_black_hole_inner_glow", "mobius_black_hole_inner_glow.spm", "0 0 0", "RotY", 90.0, 360.0),
        ("mobius_planet_black_hole_photon_ring", "mobius_black_hole_photon_ring.spm", "0 0 0", "RotY", 60.0, 360.0),
        ("mobius_planet_black_hole_core", "mobius_black_hole_core.spm", "0 0 0", "RotY", 720.0, 360.0),
    )
    for obj_id, model, hpr, channel, period, angle in black_hole_layers:
        lines.extend([
            f'  <object id="{obj_id}" type="animation" model="{model}" {black_hole_xyz} '
            f'hpr="{hpr}" scale="{black_hole_scale_attr}" interaction="ghost" shadow-pass="false">',
            f'    <curve channel="{channel}" interpolation="linear" extend="cyclic">',
            '      <p c="1.000 0.000"/>',
            f'      <p c="{period:.3f} {angle:.3f}"/>',
            '    </curve>',
            '  </object>',
        ])
    lines.extend([
        '  <checks>',
        '    <check-lap kind="lap" active="true" same-group="0" other-ids="1"/>',
        f'    <check-line kind="activate" same-group="1" other-ids="2" p1="{fmt_vec(q1_1a)}" p2="{fmt_vec(q1_1b)}"/>',
        f'    <check-line kind="activate" same-group="2" other-ids="3" p1="{fmt_vec(q2_1a)}" p2="{fmt_vec(q2_1b)}"/>',
        f'    <check-line kind="activate" same-group="3" other-ids="4" p1="{fmt_vec(q3_1a)}" p2="{fmt_vec(q3_1b)}"/>',
        f'    <check-line kind="activate" same-group="4" other-ids="5" p1="{fmt_vec(finish_2a)}" p2="{fmt_vec(finish_2b)}"/>',
        f'    <check-line kind="activate" same-group="5" other-ids="6" p1="{fmt_vec(q1_2a)}" p2="{fmt_vec(q1_2b)}"/>',
        f'    <check-line kind="activate" same-group="6" other-ids="7" p1="{fmt_vec(q2_2a)}" p2="{fmt_vec(q2_2b)}"/>',
        f'    <check-line kind="activate" same-group="7" other-ids="8" p1="{fmt_vec(q3_2a)}" p2="{fmt_vec(q3_2b)}"/>',
        f'    <check-line kind="lap" active="false" same-group="8" other-ids="1" p1="{fmt_vec(finish_1a)}" p2="{fmt_vec(finish_1b)}"/>',
        '  </checks>',
    ])
    for idx, p, heading in generated_start_positions():
        lines.append(f'  <start position="{idx}" {fmt_xyz_attrs(p)} h="{heading:.2f}"/>')
    item_boxes = [
        # Lap 1 (fewer)
        (1.18, -2.6), (5.15, -2.7),
        # Lap 2 (more)
        (6.70, -2.8), (7.46, -2.6), (8.33, -2.7), (9.30, -2.8),
        (10.48, -2.6), (10.54, 2.6), (11.43, -2.7), (11.49, 2.7)
    ]
    small_nitro = [
        # Lap 1
        (1.55, 1.8),
        # Lap 2
        (7.13, -1.8), (8.98, -1.8), (11.03, -1.8), (12.03, 1.8)
    ]
    big_nitro = [
        # Lap 2 only
        (8.23, 0.0)
    ]
    compactifications = [
        # Lap 1
        (1.35, -5.4),
        # Lap 2
        (7.23, 5.4), (7.63, -5.4), (9.53, -5.2),
        (10.33, 5.4), (10.93, -5.4), (12.28, -5.2)
    ]
    for u, lateral in item_boxes:
        lines.append(f'  <item {surface_item_attrs(u, lateral)}/>')
    for u, lateral in small_nitro:
        lines.append(f'  <small-nitro {surface_item_attrs(u, lateral)}/>')
    for u, lateral in big_nitro:
        lines.append(f'  <big-nitro {surface_item_attrs(u, lateral)}/>')
    for u, lateral in compactifications:
        lines.append(f'  <banana {surface_item_attrs(u, lateral)}/>')
    lines.append("</scene>")
    (track_dir / "scene.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_license(track_dir):
    (track_dir / "LICENSE.txt").write_text(
        "Mobius Trip procedural prototype generated by BlenderConversionScripts/generate_mobius_track.py.\n"
        "Geometry, textures, and metadata are deterministic project-local generated assets.\n"
        "Sun, black hole, accretion disk, and textures are procedural project-local generated assets.\n"
        "Planet landmark meshes/textures are imported from local source archives; see mobius_local_planets_manifest.json.\n",
        encoding="utf-8",
    )


def clear_blender_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in list(bpy.data.meshes):
        if block.users == 0:
            bpy.data.meshes.remove(block)
    for block in list(bpy.data.materials):
        if block.users == 0:
            bpy.data.materials.remove(block)


def create_blender_scene(meshes, track_dir):
    clear_blender_scene()
    materials = {
        "road": make_blender_material("Road visual material", (0.05, 0.12, 0.18, 1.0)),
        "collision": make_blender_material("Gravity collision material", (0.25, 0.25, 0.25, 0.35), True),
        "rail": make_blender_material("Rail glow material", (0.08, 0.75, 1.0, 0.85), True),
        "guardrail": make_blender_material("Guardrail panel material", (0.38, 0.62, 0.72, 1.0)),
        "sun": make_blender_material("Procedural 3D Sun material", (1.0, 0.58, 0.14, 1.0), True),
        "corona": make_blender_material("Animated Sun Corona material", (1.0, 0.48, 0.12, 0.48), True),
        "black_hole": make_blender_material("Procedural black hole core material", (0.0, 0.0, 0.0, 1.0)),
        "accretion": make_blender_material("Animated black hole accretion material", (1.0, 0.42, 0.08, 0.78), True),
        "stars": make_blender_material("Relativistic star sphere material", (0.55, 0.70, 1.0, 1.0)),
        "planet": make_blender_material("BlenderKit planet landmark material", (0.58, 0.62, 0.68, 1.0)),
        "zipper": make_blender_material("Ground zipper boost material", (1.0, 0.86, 0.18, 1.0), True),
        "reset": make_blender_material("Reset fall surface material", (0.3, 0.02, 0.18, 0.25), True),
    }
    for mesh in meshes:
        if mesh["name"].startswith("Mobius_Road_Visual"):
            material = materials["road"]
        elif mesh["name"].startswith("Mobius_Rails_Visual"):
            material = materials["rail"]
        elif mesh["name"].startswith("Mobius_Guardrails_Visual"):
            material = materials["guardrail"]
        elif mesh["name"] == "Mobius_Sun_Core":
            material = materials["sun"]
        elif mesh["name"].startswith("Mobius_Sun_Corona") or mesh["name"] == "Mobius_Sun_Prominence":
            material = materials["corona"]
        elif mesh["name"] == "Mobius_Black_Hole_Core":
            material = materials["black_hole"]
        elif mesh["name"].startswith("Mobius_Black_Hole_"):
            material = materials["accretion"]
        elif mesh["name"] == "Mobius_Relativistic_Star_Sphere":
            material = materials["stars"]
        elif mesh["name"].startswith("Mobius_Planet_"):
            material = materials["planet"]
        elif mesh["name"] == "Mobius_Zippers":
            material = materials["zipper"]
        elif mesh["name"] == "Reset_Fall_Surface":
            material = materials["reset"]
        else:
            material = materials["collision"]
        add_blender_object(mesh, material, (1, 1, 1, 1))

    light_data = bpy.data.lights.new("Mobius_Rim_Light", "POINT")
    light_data.energy = 650.0
    light_data.color = (0.5, 0.78, 1.0)
    light_obj = bpy.data.objects.new("Mobius_Rim_Light", light_data)
    light_obj.location = (0.0, 22.0, 0.0)
    bpy.context.collection.objects.link(light_obj)

    sun_data = bpy.data.lights.new("Track_Sun_Key", "SUN")
    sun_data.energy = 3.2
    sun_obj = bpy.data.objects.new("Track_Sun_Key", sun_data)
    sun_obj.rotation_euler = (math.radians(55.0), 0.0, math.radians(-38.0))
    bpy.context.collection.objects.link(sun_obj)

    camera_data = bpy.data.cameras.new("Mobius_Inspection_Camera")
    camera_obj = bpy.data.objects.new("Mobius_Inspection_Camera", camera_data)
    camera_obj.location = (130.0, 92.0, 150.0)
    camera_obj.rotation_euler = (math.radians(58.0), 0.0, math.radians(139.0))
    camera_data.lens = 24.0
    bpy.context.collection.objects.link(camera_obj)
    bpy.context.scene.camera = camera_obj

    engines = {item.identifier for item in bpy.context.scene.render.bl_rna.properties["engine"].enum_items}
    bpy.context.scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
    bpy.context.scene.world.color = (0.0, 0.0, 0.0)
    old_save_version = bpy.context.preferences.filepaths.save_version
    try:
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(track_dir / "mobius_track.blend"))
    finally:
        bpy.context.preferences.filepaths.save_version = old_save_version


def max_distance(a, b):
    return max(vlength(vsub(x, y)) for x, y in zip(a, b))


def mobius_self_check(meshes):
    seam_flipped = []
    seam_unflipped_non_center = []
    for j in range(COLLISION_V_SEGMENTS + 1):
        t = j / COLLISION_V_SEGMENTS
        v = -ROAD_HALF_WIDTH + 2.0 * ROAD_HALF_WIDTH * t
        seam_flipped.append(
            vlength(vsub(mobius_point(0.0, v), mobius_point(2.0 * math.pi, -v)))
        )
        if abs(v) > 1.0e-6:
            seam_unflipped_non_center.append(
                vlength(vsub(mobius_point(0.0, v), mobius_point(2.0 * math.pi, v)))
            )

    collision = next(mesh for mesh in meshes if mesh["name"] == "Mobius_Collision_Surface")
    row = COLLISION_V_SEGMENTS + 1
    first_row = collision["verts"][:row]
    last_row_as_next = []
    for j in range(COLLISION_V_SEGMENTS + 1):
        t = j / COLLISION_V_SEGMENTS
        v = -ROAD_HALF_WIDTH + 2.0 * ROAD_HALF_WIDTH * t
        last_row_as_next.append(mobius_point(2.0 * math.pi, -v))

    return {
        "mobius_flipped_seam_max_error": max(seam_flipped),
        "non_flipped_non_center_seam_min_error": min(seam_unflipped_non_center),
        "collision_wrapped_indices": True,
        "collision_flipped_seam_max_error": max_distance(first_row, last_row_as_next),
        "meshes": {
            mesh["name"]: {
                "vertices": len(mesh["verts"]),
                "triangles": len(mesh["indices"]) // 3,
            }
            for mesh in meshes
        },
    }


def remove_stale_generated_assets(track_dir):
    patterns = (
        "mobius_visual*.spm",
        "mobius_collision*.spm",
        "mobius_seam_bridge_collision*.spm",
        "mobius_seam_jump_ramp*.spm",
        "mobius_seam_ramp*.png",
        "mobius_safety_collision*.spm",
        "mobius_rails_visual*.spm",
        "mobius_guardrails_visual*.spm",
        "mobius_rail_collision*.spm",
        "mobius_start_gate*.spm",
        "mobius_start_gate.png",
        "mobius_minimap.png",
        "direction_marker.png",
        "direction_markers.spm",
        "mobius_sun*.spm",
        "mobius_sun_corona.png",
        "mobius_sun_core.png",
        "mobius_black_hole*.spm",
        "mobius_black_hole*.png",
        "mobius_zipper.png",
        "mobius_zippers.spm",
        "zipper.png",
        "zipper-effect.png",
        "mobius_blenderkit_realistic_sun*.spm",
        "mobius_blenderkit_realistic_sun.png",
        "mobius_blenderkit_sun_reference.json",
        "mobius_planet_*.spm",
        "mobius_planet_*.png",
        "mobius_blenderkit_planets_manifest.json",
        "mobius_local_planets_manifest.json",
        "mobius_star_sphere.spm",
        "mobius_track.blend1",
    )
    for pattern in patterns:
        for path in track_dir.glob(pattern):
            path.unlink()


def generate_mobius_track(project_root):
    project_root = Path(project_root).resolve()
    track_dir = project_root / "stk-assets" / "tracks" / "mobius_track"
    track_dir.mkdir(parents=True, exist_ok=True)
    remove_stale_generated_assets(track_dir)

    road_visual = make_strip_mesh(
        "Mobius_Road_Visual",
        "mobius_road_visual.png",
        ROAD_HALF_WIDTH,
        VISUAL_U_SEGMENTS,
        VISUAL_V_SEGMENTS,
        0,
        VISUAL_U_SEGMENTS,
        False,
    )
    rails_visual = make_rail_visual_mesh(
        "Mobius_Rails_Visual",
        0,
        VISUAL_U_SEGMENTS,
    )
    guardrail_visual = make_guardrail_visual_mesh(
        "Mobius_Guardrails_Visual",
        0,
        VISUAL_U_SEGMENTS,
    )

    collision = make_welded_mobius_surface(
        "Mobius_Collision_Surface",
        "mobius_collision.png",
        ROAD_HALF_WIDTH,
        COLLISION_U_SEGMENTS,
        COLLISION_V_SEGMENTS,
        0.0,
        False,
    )
    sync_delta = verify_mobius_visual_collision_sync(road_visual, collision)
    print(f"Mobius visual/collision road sync max delta: {sync_delta:.6g}")
    safety_collision = make_welded_mobius_surface(
        "Mobius_Safety_Collision",
        "mobius_safety_collision.png",
        ROAD_HALF_WIDTH * 1.02,
        COLLISION_U_SEGMENTS,
        COLLISION_V_SEGMENTS,
        SAFETY_SURFACE_OFFSET,
        False,
    )
    seam_bridge_collision = make_mobius_patch_mesh(
        "Mobius_Seam_Bridge_Collision",
        "mobius_collision.png",
        ROAD_HALF_WIDTH * 1.01,
        2.0 * math.pi,
        0.62,
        32,
        COLLISION_V_SEGMENTS,
        SEAM_BRIDGE_SURFACE_OFFSET,
        False,
    )
    seam_jump_ramp_collision = make_seam_jump_ramp_mesh(
        "Mobius_Seam_Jump_Ramp_Collision",
        "mobius_seam_ramp.png",
        True,
    )
    seam_jump_ramp_visual = make_seam_jump_ramp_mesh(
        "Mobius_Seam_Jump_Ramp_Visual",
        "mobius_seam_ramp_visual.png",
        False,
    )
    rail_collision = make_rail_collision_mesh()
    sun_core = make_sun_core_mesh()
    sun_corona_inner = make_sun_corona_mesh(
        "Mobius_Sun_Corona_Inner",
        SUN_CORONA_INNER_RADIUS,
        "mobius_sun_corona.png",
        math.radians(22.0),
        0.0,
    )
    sun_corona_outer = make_sun_corona_mesh(
        "Mobius_Sun_Corona_Outer",
        SUN_CORONA_OUTER_RADIUS,
        "mobius_sun_corona.png",
        math.radians(-18.0),
        1.1,
    )
    sun_prominence = make_sun_corona_mesh(
        "Mobius_Sun_Prominence",
        SUN_PROMINENCE_RADIUS,
        "mobius_sun_corona.png",
        math.radians(38.0),
        2.2,
    )
    black_hole_halo = make_black_hole_halo_mesh()
    black_hole_accretion = make_black_hole_accretion_mesh()
    black_hole_inner_glow = make_black_hole_inner_glow_mesh()
    black_hole_photon_ring = make_black_hole_photon_ring_mesh()
    black_hole_core = make_black_hole_core_mesh()
    star_sphere = make_star_sphere_mesh()
    start_gate = make_start_gate_mesh()
    zippers = make_zipper_mesh()
    reset_surface = make_reset_surface()
    record_blenderkit_black_hole_reference(track_dir)
    planet_meshes = import_local_planets(track_dir)
    meshes = [
        road_visual,
        collision,
        seam_jump_ramp_collision,
        seam_bridge_collision,
        safety_collision,
        rail_collision,
        seam_jump_ramp_visual,
        rails_visual,
        guardrail_visual,
        sun_core,
        sun_corona_inner,
        sun_corona_outer,
        sun_prominence,
        black_hole_halo,
        black_hole_accretion,
        black_hole_inner_glow,
        black_hole_photon_ring,
        black_hole_core,
        star_sphere,
        start_gate,
        zippers,
        reset_surface,
        *planet_meshes,
    ]

    create_textures(track_dir)
    write_spm(track_dir / "mobius_visual.spm", road_visual)
    write_spm(track_dir / "mobius_collision.spm", collision)
    write_spm(track_dir / "mobius_seam_jump_ramp_collision.spm", seam_jump_ramp_collision)
    write_spm(track_dir / "mobius_seam_bridge_collision.spm", seam_bridge_collision)
    write_spm(track_dir / "mobius_safety_collision.spm", safety_collision)
    write_spm(track_dir / "mobius_rails_visual.spm", rails_visual)
    write_spm(track_dir / "mobius_seam_jump_ramp_visual.spm", seam_jump_ramp_visual)
    write_spm(track_dir / "mobius_guardrails_visual.spm", guardrail_visual)
    write_spm(track_dir / "mobius_rail_collision.spm", rail_collision)
    write_spm(track_dir / "mobius_sun_core.spm", sun_core)
    write_spm(track_dir / "mobius_sun_corona_inner.spm", sun_corona_inner)
    write_spm(track_dir / "mobius_sun_corona_outer.spm", sun_corona_outer)
    write_spm(track_dir / "mobius_sun_prominence.spm", sun_prominence)
    write_spm(track_dir / "mobius_black_hole_halo.spm", black_hole_halo)
    write_spm(track_dir / "mobius_black_hole_accretion.spm", black_hole_accretion)
    write_spm(track_dir / "mobius_black_hole_inner_glow.spm", black_hole_inner_glow)
    write_spm(track_dir / "mobius_black_hole_photon_ring.spm", black_hole_photon_ring)
    write_spm(track_dir / "mobius_black_hole_core.spm", black_hole_core)
    write_spm(track_dir / "mobius_star_sphere.spm", star_sphere)
    write_spm(track_dir / "mobius_start_gate.spm", start_gate)
    write_spm(track_dir / "mobius_zippers.spm", zippers)
    write_spm(track_dir / "reset_fall_surface.spm", reset_surface)
    write_track_xml(track_dir)
    write_materials_xml(track_dir)
    write_quads_xml(track_dir)
    write_graph_xml(track_dir)
    write_scene_xml(track_dir)
    write_license(track_dir)
    create_blender_scene(meshes, track_dir)
    result = mobius_self_check(meshes)
    result["start_grid"] = start_grid_self_check()
    result["local_planets"] = [spec["id"] for spec in PLANET_SPECS]
    result["track_dir"] = str(track_dir)
    return result


if __name__ == "__main__":
    def run_generator():
        default_root = Path(__file__).resolve().parents[1]
        root = Path(globals().get("PROJECT_ROOT", os.environ.get("PROJECT_ROOT", default_root)))
        try:
            result = generate_mobius_track(root)
            print("Generated Mobius Trip:", result)
        except Exception as e:
            import traceback
            traceback.print_exc()
            print("Error generating track:", e)
        finally:
            if not bpy.app.background:
                bpy.ops.wm.quit_blender()
        return None

    if not bpy.app.background:
        bpy.app.timers.register(run_generator, first_interval=3.0)
    else:
        run_generator()
