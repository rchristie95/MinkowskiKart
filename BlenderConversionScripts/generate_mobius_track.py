#!/usr/bin/env python3
"""Generate the SuperTuxKart Mobius Track prototype.

This script is intended to run inside Blender through Blender MCP. It builds a
procedural Blender scene for inspection, writes deterministic track metadata,
creates small generated textures, and exports minimal static SPM meshes without
requiring the external STK Blender SPM exporter.
"""

from __future__ import annotations

import math
import os
import struct
from pathlib import Path

try:
    import bpy
except ImportError as exc:  # pragma: no cover - this generator is Blender-first.
    raise RuntimeError("generate_mobius_track.py must run inside Blender") from exc


RADIUS = 82.0
ROAD_HALF_WIDTH = 8.0
GRAPH_HALF_WIDTH = 5.6
COLLISION_U_SEGMENTS = 192
COLLISION_V_SEGMENTS = 12
VISUAL_U_SEGMENTS = 192
VISUAL_V_SEGMENTS = 10
SAFETY_SURFACE_OFFSET = -0.42
SUN_RADIUS = 12.0
SUN_CORONA_INNER_RADIUS = 14.2
SUN_CORONA_OUTER_RADIUS = 18.5
STAR_SPHERE_RADIUS = 360.0
SPHERE_U_SEGMENTS = 32
SPHERE_V_SEGMENTS = 16
SUN_U_SEGMENTS = 32
SUN_V_SEGMENTS = 16
SUN_CORONA_SEGMENTS = 96
THUMBNAIL_SOURCE = Path(
    os.environ.get("MOBIUS_THUMBNAIL_SOURCE", r"C:\Users\robso\Downloads\mobius.png")
)


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
            granule = 0.34 * math.sin(phi * 9.0 + theta * 5.0)
            granule += 0.22 * math.sin(phi * 17.0 - theta * 11.0)
            flare = 0.18 * math.sin(phi * 4.0 + theta * 19.0)
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


def make_star_sphere_mesh():
    return make_uv_sphere_mesh(
        "Mobius_Relativistic_Star_Sphere", "mobius_starfield.png",
        STAR_SPHERE_RADIUS, SPHERE_U_SEGMENTS, SPHERE_V_SEGMENTS, True, False)


def make_direction_markers():
    verts = []
    normals = []
    uvs = []
    indices = []
    marker_count = 18
    for m in range(marker_count):
        u = 0.33 + (2.0 * math.pi - 0.70) * m / marker_count
        p = mobius_point(u, 0.0)
        n = mobius_normal(u, 0.0)
        t = tangent_at(u)
        side = vnorm(mobius_dv(u), (1.0, 0.0, 0.0))
        center = vadd(p, vmul(n, 0.10))
        tip = vadd(center, vmul(t, 2.55))
        left = vadd(vadd(center, vmul(t, -1.25)), vmul(side, -1.45))
        right = vadd(vadd(center, vmul(t, -1.25)), vmul(side, 1.45))
        stem_left = vadd(vadd(center, vmul(t, -2.35)), vmul(side, -0.48))
        stem_right = vadd(vadd(center, vmul(t, -2.35)), vmul(side, 0.48))
        base = len(verts)
        verts.extend((tip, left, right, stem_left, stem_right))
        normals.extend((n, n, n, n, n))
        uvs.extend(((0.5, 1.0), (0.0, 0.35), (1.0, 0.35), (0.32, 0.0), (0.68, 0.0)))
        indices.extend((base, base + 1, base + 2, base + 1, base + 3, base + 4, base + 1, base + 4, base + 2))
    return mesh_dict("Direction_Markers", "direction_marker.png", verts, normals, uvs, indices)


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
        f.write(struct.pack("<B", 0x0A))  # version 1, SPMN static mesh.
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


def write_texture(path, width, height, pixel_fn, file_format="PNG"):
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

    def arrow(u, v):
        return (0.88, 1.0, 0.28, 0.86)

    def reset(u, v):
        return (0.20, 0.02, 0.18, 0.35)

    def sun_core(u, v):
        granules = 0.18 * math.sin(u * 95.0 + math.sin(v * 31.0) * 4.0)
        granules += 0.12 * math.sin((u + v) * 177.0)
        limb = 1.0 - abs(v - 0.5) * 0.8
        heat = max(0.0, min(1.0, 0.78 + granules))
        return (1.0, 0.45 + 0.30 * heat, 0.08 + 0.12 * limb, 1.0)

    def sun_corona(u, v):
        strand = 0.45 + 0.35 * math.sin(u * 47.0 + v * 9.0)
        strand += 0.20 * math.sin(u * 113.0)
        edge = math.sin(math.pi * max(0.0, min(1.0, v)))
        alpha = max(0.0, min(0.72, edge * (0.22 + 0.24 * strand)))
        return (1.0, 0.58 + 0.20 * strand, 0.16, alpha)

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

    write_texture(track_dir / "mobius_road_visual.png", 128, 128, road)
    write_texture(track_dir / "mobius_collision.png", 8, 8, collision)
    write_texture(track_dir / "mobius_safety_collision.png", 8, 8, collision)
    write_texture(track_dir / "mobius_wall_collision.png", 8, 8, wall)
    write_texture(track_dir / "mobius_rail.png", 128, 32, rail)
    write_texture(track_dir / "mobius_guardrail.png", 64, 32, guardrail)
    write_texture(track_dir / "mobius_sun_core.png", 256, 128, sun_core)
    write_texture(track_dir / "mobius_sun_corona.png", 128, 32, sun_corona)
    write_texture(track_dir / "mobius_starfield.png", 512, 256, starfield)
    write_texture(track_dir / "direction_marker.png", 64, 64, arrow)
    write_texture(track_dir / "reset_surface.png", 8, 8, reset)
    write_texture(track_dir / "screenshot.jpg", 512, 256, screenshot, file_format="JPEG")
    install_scaled_image(THUMBNAIL_SOURCE, track_dir / "screenshot.jpg", 512, 256, "JPEG")


def write_track_xml(track_dir):
    (track_dir / "track.xml").write_text(
        """<?xml version="1.0"?>
<track  name           = "Mobius Track"
        version        = "7"
        groups         = "standard"
        designer       = "Codex procedural Blender MCP generator"
        screenshot     = "screenshot.jpg"
        smooth-normals = "true"
        default-number-of-laps = "1"
        reverse        = "N"
        clouds         = "N"
        is-during-day  = "N"
        shadows        = "Y">
</track>
""",
        encoding="utf-8",
    )


def write_materials_xml(track_dir):
    (track_dir / "materials.xml").write_text(
        """<?xml version="1.0"?>
<materials>
  <material name="mobius_road_visual.png" ignore="Y"/>
  <material name="mobius_collision.png" high-adhesion="Y" has-gravity="Y"/>
  <material name="mobius_safety_collision.png" high-adhesion="Y" has-gravity="Y"/>
  <material name="mobius_wall_collision.png" high-adhesion="Y"/>
  <material name="mobius_rail.png" shader="additive" ignore="Y"/>
  <material name="mobius_guardrail.png" ignore="Y"/>
  <material name="mobius_sun_core.png" shader="additive" ignore="Y"/>
  <material name="mobius_sun_corona.png" shader="additive" ignore="Y"/>
  <material name="mobius_starfield.png" shader="additive" ignore="Y"/>
  <material name="direction_marker.png" shader="additive" ignore="Y"/>
  <material name="reset_surface.png" reset="Y" falling-effect="Y"/>
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
        '  <!-- Driveline: one-tour Mobius sprint loop -->',
    ]
    for i in range(COLLISION_U_SEGMENTS):
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
  <node-list from-quad="0" to-quad="{COLLISION_U_SEGMENTS - 1}"/>
  <edge-loop from="0" to="{COLLISION_U_SEGMENTS - 1}"/>
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


def start_position(row, col):
    u = 0.10 - row * 0.018
    lateral = -2.2 if col == 0 else 2.2
    p = item_position(u, lateral, 0.85)
    t = tangent_at(u)
    heading = math.degrees(math.atan2(t[0], t[2]))
    return p, heading


def write_scene_xml(track_dir):
    q1a, q1b = check_line_at(math.pi * 0.50)
    q2a, q2b = check_line_at(math.pi)
    q3a, q3b = check_line_at(math.pi * 1.50)
    lines = [
        '<?xml version="1.0"?>',
        '<scene>',
        '  <sky-color rgb="3 5 10"/>',
        '  <camera far="900"/>',
        '  <sun xyz="0 0 0" sun-diffuse="255 230 170" sun-specular="255 236 190" ambient="36 38 52" fog="false"/>',
        '  <track model="mobius_visual.spm" x="0" y="0" z="0">',
        '    <static-object model="mobius_star_sphere.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_safety_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_rail_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="reset_fall_surface.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="mobius_rails_visual.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="mobius_guardrails_visual.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
    ]
    lines.extend([
        '    <static-object model="direction_markers.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '  </track>',
        '  <object id="mobius_sun_core" type="animation" model="mobius_sun_core.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost" shadow-pass="false">',
        '    <curve channel="RotY" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="450.000 360.000"/>',
        '    </curve>',
        '  </object>',
        '  <object id="mobius_sun_corona_inner" type="animation" model="mobius_sun_corona_inner.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost" shadow-pass="false">',
        '    <curve channel="RotZ" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="300.000 360.000"/>',
        '    </curve>',
        '  </object>',
        '  <object id="mobius_sun_corona_outer" type="animation" model="mobius_sun_corona_outer.spm" xyz="0 0 0" hpr="18 0 0" scale="1 1 1" interaction="ghost" shadow-pass="false">',
        '    <curve channel="RotY" interpolation="linear" extend="cyclic">',
        '      <p c="1.000 0.000"/>',
        '      <p c="525.000 -360.000"/>',
        '    </curve>',
        '  </object>',
        '  <light xyz="0 0 0" id="mobius_sun_light" distance="320.00" energy="4.00" color="255 210 135" type="point"/>',
        '  <checks>',
        '    <check-lap kind="lap" same-group="0" other-ids="1"/>',
        f'    <check-line kind="activate" same-group="1" other-ids="2" p1="{fmt_vec(q1a)}" p2="{fmt_vec(q1b)}"/>',
        f'    <check-line kind="activate" same-group="2" other-ids="3" p1="{fmt_vec(q2a)}" p2="{fmt_vec(q2b)}"/>',
        f'    <check-line kind="activate" same-group="3" other-ids="0" p1="{fmt_vec(q3a)}" p2="{fmt_vec(q3b)}"/>',
        '  </checks>',
    ])
    for row in range(6):
        for col in range(2):
            idx = row * 2 + col
            p, heading = start_position(row, col)
            lines.append(f'  <start position="{idx}" {fmt_xyz_attrs(p)} h="{heading:.2f}"/>')
    item_boxes = [
        (0.42, -2.8), (0.48, 2.8), (1.18, -2.6), (1.24, 2.6),
        (2.05, -2.7), (2.11, 2.7), (3.02, -2.8), (3.08, 2.8),
        (4.20, -2.6), (4.26, 2.6), (5.15, -2.7), (5.21, 2.7),
    ]
    small_nitro = [
        (0.85, -1.8), (1.55, 1.8), (2.70, -1.8), (3.55, 1.8),
        (4.75, -1.8), (5.75, 1.8),
    ]
    big_nitro = [(1.95, 0.0), (4.95, 0.0)]
    compactifications = [
        (0.95, 5.4), (1.35, -5.4), (2.55, 5.2), (3.25, -5.2),
        (4.05, 5.4), (4.65, -5.4), (5.55, 5.2), (6.00, -5.2),
    ]
    for u, lateral in item_boxes:
        lines.append(f'  <item {fmt_xyz_attrs(item_position(u, lateral, 1.3))} drop="true"/>')
    for u, lateral in small_nitro:
        lines.append(f'  <small-nitro {fmt_xyz_attrs(item_position(u, lateral, 1.3))} drop="true"/>')
    for u, lateral in big_nitro:
        lines.append(f'  <big-nitro {fmt_xyz_attrs(item_position(u, lateral, 1.3))} drop="true"/>')
    for u, lateral in compactifications:
        lines.append(f'  <banana {fmt_xyz_attrs(item_position(u, lateral, 1.15))} drop="true"/>')
    lines.append("</scene>")
    (track_dir / "scene.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_license(track_dir):
    (track_dir / "LICENSE.txt").write_text(
        "Mobius Track procedural prototype generated by BlenderConversionScripts/generate_mobius_track.py.\n"
        "Geometry, textures, and metadata are deterministic project-local generated assets.\n"
        "Sun core, coronas, and textures are procedural project-local generated assets.\n",
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
        "stars": make_blender_material("Relativistic star sphere material", (0.55, 0.70, 1.0, 1.0)),
        "marker": make_blender_material("Direction marker material", (0.9, 1.0, 0.22, 0.9), True),
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
        elif mesh["name"].startswith("Mobius_Sun_Corona"):
            material = materials["corona"]
        elif mesh["name"] == "Mobius_Relativistic_Star_Sphere":
            material = materials["stars"]
        elif mesh["name"] == "Direction_Markers":
            material = materials["marker"]
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
    sun_data.energy = 1.8
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
    bpy.context.scene.world.color = (0.006, 0.009, 0.018)
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
        v = -ROAD_HALF_WIDTH * 0.985 + 2.0 * ROAD_HALF_WIDTH * 0.985 * t
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
        "mobius_safety_collision*.spm",
        "mobius_rails_visual*.spm",
        "mobius_guardrails_visual*.spm",
        "mobius_rail_collision*.spm",
        "mobius_sun*.spm",
        "mobius_sun_corona.png",
        "mobius_sun_core.png",
        "mobius_blenderkit_realistic_sun*.spm",
        "mobius_blenderkit_realistic_sun.png",
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
        True,
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
        ROAD_HALF_WIDTH * 0.985,
        COLLISION_U_SEGMENTS,
        COLLISION_V_SEGMENTS,
        0.0,
        True,
    )
    safety_collision = make_welded_mobius_surface(
        "Mobius_Safety_Collision",
        "mobius_safety_collision.png",
        ROAD_HALF_WIDTH * 1.02,
        COLLISION_U_SEGMENTS,
        COLLISION_V_SEGMENTS,
        SAFETY_SURFACE_OFFSET,
        True,
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
    star_sphere = make_star_sphere_mesh()
    markers = make_direction_markers()
    reset_surface = make_reset_surface()
    meshes = [
        road_visual,
        collision,
        safety_collision,
        rail_collision,
        rails_visual,
        guardrail_visual,
        sun_core,
        sun_corona_inner,
        sun_corona_outer,
        star_sphere,
        markers,
        reset_surface,
    ]

    create_textures(track_dir)
    write_spm(track_dir / "mobius_visual.spm", road_visual)
    write_spm(track_dir / "mobius_collision.spm", collision)
    write_spm(track_dir / "mobius_safety_collision.spm", safety_collision)
    write_spm(track_dir / "mobius_rails_visual.spm", rails_visual)
    write_spm(track_dir / "mobius_guardrails_visual.spm", guardrail_visual)
    write_spm(track_dir / "mobius_rail_collision.spm", rail_collision)
    write_spm(track_dir / "mobius_sun_core.spm", sun_core)
    write_spm(track_dir / "mobius_sun_corona_inner.spm", sun_corona_inner)
    write_spm(track_dir / "mobius_sun_corona_outer.spm", sun_corona_outer)
    write_spm(track_dir / "mobius_star_sphere.spm", star_sphere)
    write_spm(track_dir / "direction_markers.spm", markers)
    write_spm(track_dir / "reset_fall_surface.spm", reset_surface)
    write_track_xml(track_dir)
    write_materials_xml(track_dir)
    write_quads_xml(track_dir)
    write_graph_xml(track_dir)
    write_scene_xml(track_dir)
    write_license(track_dir)
    create_blender_scene(meshes, track_dir)
    result = mobius_self_check(meshes)
    result["track_dir"] = str(track_dir)
    return result


if __name__ == "__main__":
    default_root = Path(__file__).resolve().parents[1]
    root = Path(globals().get("PROJECT_ROOT", os.environ.get("PROJECT_ROOT", default_root)))
    result = generate_mobius_track(root)
    print("Generated Mobius Track:", result)
