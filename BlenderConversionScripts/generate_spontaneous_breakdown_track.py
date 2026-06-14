#!/usr/bin/env python3
"""Generate the game-ready Spontaneous Breakdown arena package.

This writes compact SPM meshes directly, matching the lightweight static SPM
writer used by the Mobius track generator. The high-detail Meshy GLBs remain in
SpontaneousBreakdown/ as source art; this package contains runtime-safe SPM
counterparts and low-res textures.
"""

from __future__ import annotations

import json
import math
import os
import shutil
import struct
import subprocess
import wave
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TRACK_DIR = ROOT / "stk-assets" / "tracks" / "spontaneous_breakdown"
SOURCE_DIR = ROOT / "SpontaneousBreakdown"
MESHY_TEXTURED_DIR = SOURCE_DIR / "assets" / "meshy_textured"
SOURCE_MANIFEST = SOURCE_DIR / "manifest.json"

VALLEY_R = 46.0
OUTER_R = 68.0          # quartic terrain reaches ~9.5 m here — natural boundary
PEAK_H = 15.0                     # crown height; MUST match SB_PEAK_H in three_strikes_battle.cpp
RING_A = PEAK_H / VALLEY_R ** 4   # V(r) = RING_A * (r² − VALLEY_R²)²
TAU = math.tau


ASSETS = [
    ("donkey_hazard", "dynamic_sphere", "Buridan's Ass hazard"),
    ("goldstone_boson_hazard", "kinematic_orbit_sphere", "Goldstone boson hazard"),
]

# The donkey and goldstone runtime meshes/textures come from the high-detail
# Meshy GLBs (converted to SPM by the Blender step), NOT the procedural builders.
# They stay in ASSETS so materials.xml still emits their material, but their
# mesh + texture are skipped here so a regen does not clobber the GLB versions.
GLB_ASSET_STEMS = {"donkey_hazard", "goldstone_boson_hazard"}

UNUSED_RUNTIME_ASSET_STEMS = [
    "accelerator_power_node",
    "broken_detector_debris",
    "bumper_cyan_proton",
    "bumper_purple_wedge",
    "bumper_red_magnet",
    "bumper_triangle_stripes",
    "cable_coil_blue_plugs",
    "collider_support_pylon",
    "detector_wall_curved_blue",
    "detector_wall_green_strips",
    "detector_wall_magenta_corner",
    "detector_wall_red_blocks",
    "donkey_impact_marker",
    "physics_decal_feynman",
    "physics_decal_mexican_hat",
    "physics_decal_symbols",
    "ramp_split_fork",
    "ramp_wedge_arrow",
    "sb_outer_collider_wall",
    "sign_bray_impact",
    "sign_goldstone_orbit",
    "sign_unstable_center",
    "sign_vacuum_valley",
    "symmetry_glyph_emitter",
]

PALETTE = {
    "donkey_hazard": ((0.72, 0.55, 0.38), (0.20, 0.12, 0.08), (0.95, 0.84, 0.58)),
    "goldstone_boson_hazard": ((1.00, 0.76, 0.12), (1.00, 0.95, 0.32), (0.35, 0.20, 0.02)),
    "bumper_triangle_stripes": ((0.95, 0.16, 0.10), (0.04, 0.04, 0.05), (1.00, 0.92, 0.20)),
    "bumper_red_magnet": ((0.88, 0.08, 0.08), (0.10, 0.10, 0.12), (0.95, 0.95, 0.92)),
    "bumper_cyan_proton": ((0.05, 0.78, 0.95), (0.02, 0.08, 0.12), (0.75, 1.00, 1.00)),
    "bumper_purple_wedge": ((0.58, 0.18, 0.95), (0.07, 0.05, 0.12), (1.00, 0.54, 0.98)),
    "sign_unstable_center": ((1.00, 0.30, 0.12), (0.08, 0.07, 0.06), (1.00, 0.95, 0.20)),
    "sign_vacuum_valley": ((0.10, 0.95, 0.85), (0.02, 0.08, 0.10), (0.70, 1.00, 0.92)),
    "sign_bray_impact": ((0.95, 0.70, 0.22), (0.08, 0.06, 0.03), (0.22, 0.15, 0.10)),
    "sign_goldstone_orbit": ((1.00, 0.82, 0.12), (0.08, 0.08, 0.02), (0.15, 0.75, 1.00)),
    "ramp_wedge_arrow": ((0.24, 0.62, 0.98), (0.02, 0.04, 0.08), (1.00, 0.95, 0.16)),
    "ramp_split_fork": ((0.16, 0.86, 0.55), (0.02, 0.08, 0.05), (1.00, 0.64, 0.14)),
    "cable_coil_blue_plugs": ((0.06, 0.28, 0.88), (0.01, 0.02, 0.06), (0.90, 0.95, 1.00)),
    "broken_detector_debris": ((0.50, 0.54, 0.58), (0.08, 0.08, 0.08), (0.94, 0.32, 0.18)),
    "accelerator_power_node": ((0.05, 0.88, 0.86), (0.02, 0.07, 0.08), (1.00, 0.92, 0.25)),
    "physics_decal_symbols": ((0.25, 0.95, 1.00), (0.02, 0.04, 0.08), (1.00, 0.55, 0.95)),
    "physics_decal_mexican_hat": ((1.00, 0.72, 0.12), (0.08, 0.04, 0.01), (0.30, 0.95, 1.00)),
    "physics_decal_feynman": ((0.92, 0.92, 1.00), (0.02, 0.02, 0.06), (0.40, 1.00, 0.65)),
    "symmetry_glyph_emitter": ((0.70, 0.18, 1.00), (0.02, 0.00, 0.05), (0.20, 0.98, 1.00)),
    "collider_support_pylon": ((0.35, 0.40, 0.48), (0.05, 0.06, 0.08), (0.94, 0.70, 0.18)),
    "donkey_impact_marker": ((1.00, 0.58, 0.10), (0.08, 0.04, 0.01), (0.92, 0.16, 0.12)),
}


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vcross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vnorm(v):
    l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if l <= 1.0e-7:
        return (0.0, 1.0, 0.0)
    return (v[0] / l, v[1] / l, v[2] / l)


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def ring_height(r):
    return RING_A * (r * r - VALLEY_R * VALLEY_R) ** 2


class MeshBuilder:
    def __init__(self, name, texture):
        self.name = name
        self.texture = texture
        self.verts = []
        self.normals = []
        self.uvs = []
        self.indices = []

    def quad(self, p0, p1, p2, p3, uv0=(0, 0), uv1=(1, 0), uv2=(1, 1), uv3=(0, 1)):
        n = vnorm(vcross(vsub(p1, p0), vsub(p2, p0)))
        base = len(self.verts)
        self.verts.extend([p0, p1, p2, p3])
        self.normals.extend([n, n, n, n])
        self.uvs.extend([uv0, uv1, uv2, uv3])
        self.indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    def tri(self, p0, p1, p2, uv0=(0, 0), uv1=(1, 0), uv2=(0.5, 1)):
        n = vnorm(vcross(vsub(p1, p0), vsub(p2, p0)))
        base = len(self.verts)
        self.verts.extend([p0, p1, p2])
        self.normals.extend([n, n, n])
        self.uvs.extend([uv0, uv1, uv2])
        self.indices.extend([base, base + 1, base + 2])

    def box(self, cx, cy, cz, sx, sy, sz):
        x0, x1 = cx - sx / 2, cx + sx / 2
        y0, y1 = cy - sy / 2, cy + sy / 2
        z0, z1 = cz - sz / 2, cz + sz / 2
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0))
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1))
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))

    def as_dict(self):
        return {
            "name": self.name,
            "texture": self.texture,
            "verts": self.verts,
            "normals": self.normals,
            "uvs": self.uvs,
            "indices": self.indices,
        }


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
        f.write(struct.pack("<B", 0x0A))
        f.write(struct.pack("<B", 0x01))
        f.write(struct.pack("<6f", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]))
        f.write(struct.pack("<H", 1))
        f.write(struct.pack("<B", len(texture)))
        f.write(texture)
        f.write(struct.pack("<B", 0))
        f.write(struct.pack("<H", 1))
        f.write(struct.pack("<H", 1))
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


def write_png(path, width, height, pixel_fn):
    def chunk(tag, data):
        payload = tag + data
        return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)

    rows = []
    for y in range(height):
        v = y / max(1, height - 1)
        row = bytearray([0])
        for x in range(width):
            u = x / max(1, width - 1)
            r, g, b, a = pixel_fn(u, v)
            row.extend([
                int(max(0, min(255, r * 255))),
                int(max(0, min(255, g * 255))),
                int(max(0, min(255, b * 255))),
                int(max(0, min(255, a * 255))),
            ])
        rows.append(bytes(row))
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def mix(a, b, t):
    return tuple(a[i] * (1.0 - t) + b[i] * t for i in range(3))


def asset_texture(name):
    base, dark, accent = PALETTE[name]
    out = TRACK_DIR / f"{name}.png"

    def px(u, v):
        grid = 0.10 if (int(u * 8) == int(u * 8 + 0.08) or int(v * 8) == int(v * 8 + 0.08)) else 0.0
        stripe = 0.55 if ((u + v) * 9.0) % 1.0 < 0.18 else 0.0
        ring = abs(math.sin((u * u + v * v) * 18.0))
        color = mix(dark, base, 0.72 + 0.18 * ring)
        if stripe:
            color = mix(color, accent, stripe)
        color = mix(color, (1.0, 1.0, 1.0), grid)
        return color[0], color[1], color[2], 1.0

    write_png(out, 96, 96, px)


def runtime_asset_texture(name):
    source = MESHY_TEXTURED_DIR / f"{name}_base_color.png"
    if source.exists():
        shutil.copy2(source, TRACK_DIR / f"{name}.png")
    else:
        asset_texture(name)


def arena_texture():
    def px(u, v):
        dx, dy = u - 0.5, v - 0.5
        r = math.sqrt(dx * dx + dy * dy) * 2.0
        a = math.atan2(dy, dx)
        valley = 1.0 - min(1.0, abs(r - 0.62) / 0.12)
        grid = 1.0 if abs((r * 18.0) % 1.0 - 0.5) < 0.025 or abs((a * 10.0 / TAU) % 1.0 - 0.5) < 0.025 else 0.0
        c = mix((0.06, 0.08, 0.11), (0.12, 0.20, 0.26), min(1.0, r))
        c = mix(c, (0.05, 0.85, 0.95), valley * 0.55)
        c = mix(c, (1.0, 0.75, 0.08), grid * 0.45)
        return c[0], c[1], c[2], 1.0

    write_png(TRACK_DIR / "sb_arena_surface.png", 256, 256, px)


def utility_textures():
    arena_texture()
    write_png(TRACK_DIR / "sb_collision.png", 8, 8, lambda u, v: (0.02, 0.02, 0.025, 1.0))
    # Only generate procedural screenshot if no hand-crafted one exists.
    ss_path = TRACK_DIR / "spontaneous_breakdown_screenshot.png"
    if not ss_path.exists():
        write_png(ss_path, 1024, 576, screenshot_px)
    for name, _, _ in ASSETS:
        if name in GLB_ASSET_STEMS:
            continue  # GLB-derived texture written by the Blender conversion step
        runtime_asset_texture(name)


def screenshot_px(u, v):
    x, z = u - 0.5, v - 0.5
    r = math.sqrt(x * x + z * z) * 2.2
    valley = max(0.0, 1.0 - abs(r - 0.62) / 0.08)
    center = max(0.0, 1.0 - r / 0.22)
    outer_slope = max(0.0, min(1.0, (r - 0.72) / 0.20))   # fade to steep rim colour
    c = mix((0.02, 0.03, 0.07), (0.06, 0.09, 0.14), min(1, r))
    c = mix(c, (0.04, 0.04, 0.08), outer_slope)
    c = mix(c, (0.05, 0.88, 0.95), valley)
    c = mix(c, (0.95, 0.25, 0.12), center)
    if abs(u - 0.5) < 0.006 or abs(v - 0.5) < 0.006:
        c = mix(c, (1.0, 0.82, 0.15), 0.55)
    return c[0], c[1], c[2], 1.0


def make_arena_mesh():
    # Cluster rings near valley and centre for smooth quartic curvature
    # Dense, evenly spaced rings so the deeper quartic curve reads smooth (no
    # faceted bands), with extra clustering around the crown and valley trough.
    rings = [0.0, 3.0, 6.0, 9.0, 12.0, 16.0, 20.0, 24.0, 28.0, 32.0, 36.0,
             40.0, 43.0, VALLEY_R, 49.0, 52.0, 56.0, 60.0, 64.0, OUTER_R]
    segs = 96
    verts, normals, uvs, indices = [], [], [], []
    for ri, r in enumerate(rings):
        for si in range(segs):
            a = TAU * si / segs
            x, z = math.cos(a) * r, math.sin(a) * r
            y = ring_height(r)
            # dV/dr = 4*RING_A*r*(r²-VALLEY_R²)
            dr = 4.0 * RING_A * r * (r * r - VALLEY_R * VALLEY_R)
            normals.append(vnorm((-dr * math.cos(a), 1.0, -dr * math.sin(a))))
            verts.append((x, y, z))
            uvs.append((0.5 + x / (OUTER_R * 2.0), 0.5 + z / (OUTER_R * 2.0)))
    for ri in range(len(rings) - 1):
        for si in range(segs):
            a = ri * segs + si
            b = ri * segs + (si + 1) % segs
            c = (ri + 1) * segs + (si + 1) % segs
            d = (ri + 1) * segs + si
            indices.extend([a, b, c, a, c, d])
    return {"name": "Spontaneous_Breakdown_Arena", "texture": "sb_arena_surface.png",
            "verts": verts, "normals": normals, "uvs": uvs, "indices": indices}


def make_box_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    b.box(0, 0.5, 0, 1.2, 1.0, 0.55)
    return b.as_dict()


def make_sign_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    b.box(0, 1.65, 0, 2.2, 0.95, 0.12)
    b.box(-0.75, 0.65, 0, 0.14, 1.2, 0.14)
    b.box(0.75, 0.65, 0, 0.14, 1.2, 0.14)
    return b.as_dict()


def make_ramp_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    p = [(-1.0, 0.0, -0.65), (1.0, 0.0, -0.65), (1.0, 0.0, 0.65), (-1.0, 0.0, 0.65),
         (-1.0, 0.55, 0.65), (1.0, 0.55, 0.65)]
    b.quad(p[0], p[1], p[2], p[3])
    b.quad(p[3], p[2], p[5], p[4])
    b.tri(p[0], p[3], p[4])
    b.tri(p[1], p[5], p[2])
    b.quad(p[0], p[4], p[5], p[1])
    if "split" in name:
        b.box(-0.55, 0.62, 0.40, 0.18, 0.22, 0.70)
        b.box(0.55, 0.62, 0.40, 0.18, 0.22, 0.70)
    return b.as_dict()


def make_octa_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    top, bottom = (0, 0.95, 0), (0, -0.95, 0)
    pts = [(1, 0, 0), (0, 0, 1), (-1, 0, 0), (0, 0, -1)]
    for i in range(4):
        b.tri(top, pts[i], pts[(i + 1) % 4])
        b.tri(bottom, pts[(i + 1) % 4], pts[i])
    return b.as_dict()


def make_donkey_asset():
    b = MeshBuilder("donkey_hazard", "donkey_hazard.png")
    b.box(0, 0.65, 0, 1.4, 0.55, 0.65)
    b.box(0.82, 0.82, 0, 0.50, 0.38, 0.42)
    b.box(1.02, 1.15, -0.13, 0.10, 0.42, 0.08)
    b.box(1.02, 1.15, 0.13, 0.10, 0.42, 0.08)
    for x in (-0.42, 0.42):
        for z in (-0.22, 0.22):
            b.box(x, 0.20, z, 0.16, 0.42, 0.16)
    b.box(-0.86, 0.75, 0, 0.40, 0.09, 0.09)
    return b.as_dict()


def make_cylinder_asset(name, radius=0.5, height=1.6, sides=8):
    b = MeshBuilder(name, f"{name}.png")
    top = (0, height, 0)
    bottom = (0, 0, 0)
    for i in range(sides):
        a0, a1 = TAU * i / sides, TAU * (i + 1) / sides
        p0 = (math.cos(a0) * radius, 0, math.sin(a0) * radius)
        p1 = (math.cos(a1) * radius, 0, math.sin(a1) * radius)
        p2 = (math.cos(a1) * radius, height, math.sin(a1) * radius)
        p3 = (math.cos(a0) * radius, height, math.sin(a0) * radius)
        b.quad(p0, p1, p2, p3)
        b.tri(top, p3, p2)
        b.tri(bottom, p1, p0)
    return b.as_dict()


def make_decal_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    b.quad((-1.2, 0.02, -1.2), (1.2, 0.02, -1.2), (1.2, 0.02, 1.2), (-1.2, 0.02, 1.2))
    return b.as_dict()


def make_cable_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    sides = 8
    for i in range(sides):
        a = TAU * i / sides
        b.box(math.cos(a) * 0.55, 0.14, math.sin(a) * 0.55, 0.32, 0.28, 0.18)
    b.box(-0.95, 0.14, 0, 0.35, 0.24, 0.24)
    b.box(0.95, 0.14, 0, 0.35, 0.24, 0.24)
    return b.as_dict()


def make_debris_asset(name):
    b = MeshBuilder(name, f"{name}.png")
    b.box(-0.45, 0.22, -0.2, 0.75, 0.35, 0.42)
    b.box(0.30, 0.16, 0.25, 0.45, 0.28, 0.35)
    b.box(0.65, 0.32, -0.18, 0.25, 0.56, 0.24)
    return b.as_dict()


def make_mesh_for_asset(name):
    if name == "donkey_hazard":
        return make_donkey_asset()
    if name == "goldstone_boson_hazard":
        return make_octa_asset(name)
    if name.startswith("sign_"):
        return make_sign_asset(name)
    if name.startswith("ramp_"):
        return make_ramp_asset(name)
    if name.startswith("physics_decal") or name == "donkey_impact_marker":
        return make_decal_asset(name)
    if name == "cable_coil_blue_plugs":
        return make_cable_asset(name)
    if name == "broken_detector_debris":
        return make_debris_asset(name)
    if name in ("accelerator_power_node", "collider_support_pylon", "symmetry_glyph_emitter"):
        return make_cylinder_asset(name)
    return make_box_asset(name)


def y_at_radius(r):
    return ring_height(r) + 0.08


def scene_object(asset, idx, angle_deg, radius, y, scale, interaction="ghost", shape="box", extra=""):
    a = math.radians(angle_deg)
    x, z = math.cos(a) * radius, math.sin(a) * radius
    yaw = 90.0 - angle_deg
    return (f'  <object id="{asset}.{idx:03d}" type="animation" model="{asset}.spm" '
            f'xyz="{x:.3f} {y:.3f} {z:.3f}" hpr="0.0 {yaw:.2f} 0.0" scale="{scale}" '
            f'interaction="{interaction}" shape="{shape}" skeletal-animation="false" {extra}/>\n')


def write_scene_xml():
    lines = ['<?xml version="1.0"?>\n', '<scene>\n']
    lines += [
        '  <sky-color rgb="3 6 13"/>\n',
        '  <camera far="650"/>\n',
        '  <sun xyz="-40 110 55" sun-diffuse="220 230 255" sun-specular="255 240 200" ambient="110 115 130" fog="true" fog-color="4 8 16" fog-start="70" fog-end="360"/>\n',
        '  <light xyz="46.0 5.0 0.0" id="vl0" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="23.0 5.0 39.8" id="vl1" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="-23.0 5.0 39.8" id="vl2" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="-46.0 5.0 0.0" id="vl3" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="-23.0 5.0 -39.8" id="vl4" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="23.0 5.0 -39.8" id="vl5" distance="60.0" energy="2.0" color="200 210 255"/>\n',
        '  <light xyz="0.0 10.0 0.0" id="vl6" distance="40.0" energy="1.5" color="255 220 160"/>\n',
        '  <track model="spontaneous_breakdown_arena.spm" x="0" y="0" z="0">\n',
        '  </track>\n',
    ]

    # (Jump ramps removed.)

    # Signs sit just outside the valley on the gentle outer slope (r=52: V≈0.54 m)
    lines += [
        # y = ring_height(VALLEY_R) + octahedron_bound_radius(1.0)*scale(2.4) + hover(1.0)
        # = 0 + 2.4 + 1.0 = 3.4, a constant-height circle that clearly rides above
        # the (radially symmetric, flat-at-r=46) valley floor with 1.0 m clearance.
        # Kept in sync with updateSpontaneousBreakdownEvents(). The golden-glow
        # GLB carries its own baked colour, so no engine glow/forcedbloom here.
        '  <object id="sb_goldstone_boson" type="animation" model="goldstone_boson_hazard.spm" xyz="46.000 3.400 0.000" hpr="0 0 0" scale="2.4 2.4 2.4" interaction="explode" explode="y" shape="sphere" radius="2.2" skeletal-animation="false"/>\n',
        # Buridan's donkey rests on the unstable central crown (ring_height(0)=PEAK_H).
        # C++ repositions it to getSpontaneousBreakdownHeight(0)+0.95 each tick; match here.
        f'  <object id="sb_buridan_donkey" type="animation" model="donkey_hazard.spm" xyz="0.000 {ring_height(0.0)+0.95:.3f} 0.000" hpr="0 0 0" scale="2.4 2.4 2.4" interaction="explode" explode="y" shape="sphere" radius="2.5" skeletal-animation="false"/>\n',
        '  <object id="sb_bray_sfx" type="sfx-emitter" sound="donkey_bray.ogg" rolloff="0.35" volume="1.0" max_dist="125.0" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" conditions="manual"/>\n',
    ]
    for i in range(10):
        deg = i * 36.0 + 8.0
        a = math.radians(deg)
        r = 34.0 if i % 2 == 0 else 38.0
        lines.append(f'  <start position="{i}" x="{math.cos(a)*r:.3f}" y="{ring_height(r)+1.25:.3f}" z="{math.sin(a)*r:.3f}" h="{90.0-deg:.2f}"/>\n')
    for i in range(12):
        deg = i * 30.0 + 15.0
        a = math.radians(deg)
        r = 38.0 + 12.0 * (i % 2)
        tag = "big-nitro" if i % 4 == 0 else "item"
        lines.append(f'  <{tag} x="{math.cos(a)*r:.3f}" y="{ring_height(r)+0.65:.3f}" z="{math.sin(a)*r:.3f}" drop="false"/>\n')
    lines.append('</scene>\n')
    (TRACK_DIR / "scene.xml").write_text("".join(lines), encoding="utf-8")


def write_track_xml():
    (TRACK_DIR / "track.xml").write_text("""<?xml version="1.0"?>
<track  name           = "Spontaneous Breakdown"
        version        = "7"
        groups         = "standard minkowski"
        designer       = "Robson Christie"
        music          = "highway_gravel.music"
        arena          = "Y"
        max-arena-players = "10"
        screenshot     = "spontaneous_breakdown_screenshot.png"
        smooth-normals = "false"
        default-number-of-laps = "3"
        reverse        = "N"
        clouds         = "N"
        is-during-day  = "N"
        shadows        = "Y">
</track>
""", encoding="utf-8")


def write_materials_xml():
    lines = ['<?xml version="1.0"?>\n', '<materials>\n']
    lines += [
        '  <material name="sb_arena_surface.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>\n',
        '  <material name="sb_collision.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>\n',
    ]
    for name, collision, _ in ASSETS:
        ignore = ' ignore="Y"' if collision == "ghost" else ""
        shader = ' shader="additive"' if name in ("symmetry_glyph_emitter", "physics_decal_symbols", "physics_decal_mexican_hat", "physics_decal_feynman") else ""
        lines.append(f'  <material name="{name}.png"{shader}{ignore} backface-culling="N"/>\n')
    lines.append('</materials>\n')
    (TRACK_DIR / "materials.xml").write_text("".join(lines), encoding="utf-8")


def write_navmesh_xml():
    segs = 32
    # Outer navmesh ring at r=55 where V≈1.3 m — still accessible but marking
    # the start of the steepening outer slope (natural boundary replaces walls).
    radii = [8.0, 22.0, 36.0, 46.0, 55.0]
    verts = []
    for r in radii:
        for i in range(segs):
            a = TAU * i / segs
            verts.append((math.cos(a) * r, ring_height(r), math.sin(a) * r))

    def vid(ri, si):
        return ri * segs + (si % segs)

    faces = []
    face_index = {}
    for ri in range(len(radii) - 1):
        for si in range(segs):
            face_index[(ri, si)] = len(faces)
            # Graph::createQuad negates the raw triangle normal, so this
            # winding intentionally produces raw downward normals.
            faces.append((vid(ri, si), vid(ri + 1, si), vid(ri + 1, si + 1), vid(ri, si + 1)))

    lines = ['<?xml version="1.0" encoding="utf-8"?>\n', '<navmesh>\n',
             '<height-testing min="-4.000000" max="14.000000"/>\n',
             '<MaxVertsPerPoly nvp="4" />\n', '<vertices>\n']
    for x, y, z in verts:
        lines.append(f'<vertex x="{x:.6f}" y="{y:.6f}" z="{z:.6f}" />\n')
    lines.append('</vertices>\n<faces>\n')
    for ri in range(len(radii) - 1):
        for si in range(segs):
            adj = [face_index[(ri, (si - 1) % segs)], face_index[(ri, (si + 1) % segs)]]
            if ri > 0:
                adj.append(face_index[(ri - 1, si)])
            if ri < len(radii) - 2:
                adj.append(face_index[(ri + 1, si)])
            lines.append(f'<face indices="{" ".join(str(x) for x in faces[face_index[(ri, si)]])} " adjacents="{" ".join(str(x) for x in adj)} " />\n')
    lines.append('</faces>\n</navmesh>\n')
    (TRACK_DIR / "navmesh.xml").write_text("".join(lines), encoding="utf-8")


def write_manifest():
    source_assets = {}
    if SOURCE_MANIFEST.exists():
        data = json.loads(SOURCE_MANIFEST.read_text(encoding="utf-8"))
        for item in data.get("assets", []):
            source_assets[item.get("name", "")] = item
    runtime_assets = []
    for name, collision, role in ASSETS:
        src = source_assets.get(name, {})
        spm = TRACK_DIR / f"{name}.spm"
        runtime_assets.append({
            "name": name,
            "source": "Meshy AI MCP retextured GLB; runtime SPM counterpart generated locally",
            "meshy_task_id": src.get("meshy_task_id"),
            "meshy_retexture_task_id": src.get("meshy_retexture_task_id"),
            "meshy_credit_cost": src.get("meshy_credit_cost", 20) + 10,
            "source_glb": str(MESHY_TEXTURED_DIR / f"{name}.glb"),
            "runtime_spm": str(spm),
            "runtime_texture": str(TRACK_DIR / f"{name}.png"),
            "estimated_polygon_count": len(make_mesh_for_asset(name)["indices"]) // 3,
            "scale": src.get("scale", [1, 1, 1]),
            "collision_type": collision,
            "intended_gameplay_role": src.get("intended_gameplay_role", role),
        })
    manifest = {
        "map_name": "Spontaneous Breakdown",
        "runtime_track_path": str(TRACK_DIR),
        "blender_scene": str(SOURCE_DIR / "Spontaneous_Breakdown.blend"),
        "art_exports": {
            "glb": None,
            "fbx": None,
            "note": "Deleted locally to save disk; regenerate from the Blender source scene if needed.",
        },
        "meshy_budget": {
            "hard_cap_credits": 1500,
            "actual_generation_credits": 500,
            "actual_retexture_credits": 250,
            "actual_total_credits": 750,
            "remaining_credits": 750,
        },
        "arena": {
            "source": "Blender/procedural primitive mesh for runtime",
            "runtime_spm": str(TRACK_DIR / "spontaneous_breakdown_arena.spm"),
            "estimated_polygon_count": len(make_arena_mesh()["indices"]) // 3,
            "collision_type": "driveable_static_mesh",
            "scale": [1, 1, 1],
            "intended_gameplay_role": "Higgs Mexican-hat arena with unstable peak, slopes, and circular degenerate vacuum valley battle track.",
        },
        "scripted_events": [
            "Goldstone boson orbits the vacuum valley at constant radius.",
            "Buridan's Ass respawns at the unstable center, hesitates, rolls downhill in a random direction, and triggers donkey_bray.ogg on impact.",
        ],
        "assets": runtime_assets,
    }
    (TRACK_DIR / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def write_notes():
    (TRACK_DIR / "SETUP_NOTES.md").write_text("""# Spontaneous Breakdown

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
""", encoding="utf-8")
    (TRACK_DIR / "licenses.txt").write_text("""Spontaneous Breakdown

Runtime meshes/textures generated locally for this project.
Meshy AI source assets generated and retextured through Meshy AI MCP for this user request.
Synthetic donkey_bray.ogg generated locally for the Buridan's Ass impact event.
Project/game code remains under the repository's existing license.
""", encoding="utf-8")


def write_donkey_bray_wav():
    path = TRACK_DIR / "donkey_bray.wav"
    rate = 44100
    duration = 1.25
    samples = int(rate * duration)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(samples):
            t = i / rate
            env = min(1.0, t * 10.0) * max(0.0, 1.0 - (t / duration) ** 1.8)
            sweep = 240.0 + 90.0 * math.sin(t * 13.0) + 130.0 * max(0.0, math.sin(t * 5.0))
            tone = math.sin(TAU * sweep * t) + 0.35 * math.sin(TAU * (sweep * 1.9) * t)
            wobble = math.sin(TAU * 18.0 * t) * 0.25
            sample = int(max(-1.0, min(1.0, (tone + wobble) * 0.33 * env)) * 32767)
            frames.extend(struct.pack("<h", sample))
        w.writeframes(frames)


def encode_donkey_bray_ogg():
    wav_path = TRACK_DIR / "donkey_bray.wav"
    ogg_path = TRACK_DIR / "donkey_bray.ogg"
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return
    subprocess.run([
        ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(wav_path), "-ac", "1", "-ar", "44100",
        "-codec:a", "libvorbis", "-q:a", "4", str(ogg_path),
    ], check=True)
    wav_path.unlink(missing_ok=True)


def write_all_meshes():
    write_spm(TRACK_DIR / "spontaneous_breakdown_arena.spm", make_arena_mesh())
    for name, _, _ in ASSETS:
        if name in GLB_ASSET_STEMS:
            continue  # GLB-derived; written by the Blender conversion step
        write_spm(TRACK_DIR / f"{name}.spm", make_mesh_for_asset(name))


def cleanup_unused_runtime_assets():
    for stem in UNUSED_RUNTIME_ASSET_STEMS:
        for ext in (".spm", ".png"):
            (TRACK_DIR / f"{stem}{ext}").unlink(missing_ok=True)


def main():
    TRACK_DIR.mkdir(parents=True, exist_ok=True)
    utility_textures()
    write_all_meshes()
    cleanup_unused_runtime_assets()
    write_track_xml()
    write_materials_xml()
    write_scene_xml()
    write_navmesh_xml()
    write_donkey_bray_wav()
    encode_donkey_bray_ogg()
    write_manifest()
    write_notes()
    print(f"Generated {TRACK_DIR}")
    print("Meshy budget: 750 / 1500 credits used, 750 remaining.")


if __name__ == "__main__":
    os.chdir(ROOT)
    main()
