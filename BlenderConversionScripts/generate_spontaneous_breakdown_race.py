#!/usr/bin/env python3
"""Generate a RACING variant of the Spontaneous Breakdown track.

Karts race around the circular vacuum valley (r = VALLEY_R) of the Mexican-hat
arena -- the banked trough is a natural circular circuit. Four arches stand in
the valley at 90-degree spacing; a kart must pass through all four in order to
register a lap. The arch at the start/finish line is the lap gate.

This reuses the battle track's arena mesh + textures (the geometry is identical;
only the driveline, lap gates and race metadata differ). Run the battle
generator first so the arena assets exist to copy.
"""
from __future__ import annotations

import math
import shutil
from pathlib import Path

import generate_spontaneous_breakdown_track as sb

ROOT = sb.ROOT
SRC_DIR = sb.TRACK_DIR                                   # battle track (asset source)
RACE_DIR = ROOT / "stk-assets" / "tracks" / "spontaneous_breakdown_race"

VALLEY_R = sb.VALLEY_R                                   # 46.0
ring_height = sb.ring_height
TAU = 2.0 * math.pi

# --- driveline ribbon around the valley -------------------------------------
DRIVE_R_IN = 40.0
DRIVE_R_OUT = 52.0
N_SEG = 144

# --- lap gates --------------------------------------------------------------
# Gate order encountered while driving CCW from the start line (angle 0).
# g1=90, g2=180, g3=270, g4=0 (the lap/finish gate, just behind the grid).
GATE_ANGLES_DEG = [90.0, 180.0, 270.0, 0.0]
START_ANGLE_DEG = 8.0                                    # grid sits just past the finish gate
CHECK_R_IN = 37.0
CHECK_R_OUT = 55.0

# --- arch geometry ----------------------------------------------------------
ARCH_R = 8.0          # semicircle radius (half-span); spans r=38..54 around r=46
ARCH_BAND = 1.4       # radial thickness of the arch bar
ARCH_DEPTH = 1.6      # gate depth along the driving direction
ARCH_SEGS = 26


def fmt_vec(v):
    return f"{v[0]:.3f} {v[1]:.3f} {v[2]:.3f}"


def fmt_xyz(v):
    return f'x="{v[0]:.3f}" y="{v[1]:.3f}" z="{v[2]:.3f}"'


def ring_pt(r, a_deg):
    a = math.radians(a_deg)
    return (math.cos(a) * r, ring_height(r), math.sin(a) * r)


# ---------------------------------------------------------------------------
# Driveline + graph
# ---------------------------------------------------------------------------
def write_quads_xml():
    lines = [
        '<?xml version="1.0"?>',
        '<quads>',
        '  <height-testing min="-30.000000" max="30.000000"/>',
        '  <!-- Circular driveline around the vacuum valley (CCW) -->',
    ]

    def pt(r, a):
        return (math.cos(a) * r, ring_height(r), math.sin(a) * r)

    for i in range(N_SEG):
        a0 = TAU * i / N_SEG
        a1 = TAU * (i + 1) / N_SEG
        p0 = pt(DRIVE_R_IN, a0)
        p1 = pt(DRIVE_R_OUT, a0)
        p2 = pt(DRIVE_R_OUT, a1)
        p3 = pt(DRIVE_R_IN, a1)
        lines.append(
            f'  <quad p0="{fmt_vec(p0)}" p1="{fmt_vec(p1)}" '
            f'p2="{fmt_vec(p2)}" p3="{fmt_vec(p3)}"/>'
        )
    lines.append('</quads>')
    (RACE_DIR / "quads.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_graph_xml():
    (RACE_DIR / "graph.xml").write_text(
        f'<?xml version="1.0"?>\n<graph>\n'
        f'  <node-list from-quad="0" to-quad="{N_SEG - 1}"/>\n'
        f'  <edge-loop from="0" to="{N_SEG - 1}"/>\n'
        f'</graph>\n',
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# Arch meshes (semicircular gates straddling the driveline)
# ---------------------------------------------------------------------------
def _add_quad(mesh, a, b, c, d):
    base = len(mesh["verts"])
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (d[0] - a[0], d[1] - a[1], d[2] - a[2])
    n = (e1[1] * e2[2] - e1[2] * e2[1],
         e1[2] * e2[0] - e1[0] * e2[2],
         e1[0] * e2[1] - e1[1] * e2[0])
    for p, uv in ((a, (0.0, 0.0)), (b, (1.0, 0.0)), (c, (1.0, 1.0)), (d, (0.0, 1.0))):
        mesh["verts"].append(p)
        mesh["normals"].append(n)
        mesh["uvs"].append(uv)
    mesh["indices"].extend([base, base + 1, base + 2, base, base + 2, base + 3])


def _arch_into(mesh, a_deg, base_y=0.0):
    a = math.radians(a_deg)
    ux, uz = math.cos(a), math.sin(a)     # radial unit (gate width axis)
    tx, tz = -math.sin(a), math.cos(a)    # tangent unit (driving / depth axis)
    ri, ro = ARCH_R - ARCH_BAND, ARCH_R
    hd = ARCH_DEPTH * 0.5

    def world(s, h, d):
        rr = VALLEY_R + s
        return (rr * ux + d * tx, base_y + h, rr * uz + d * tz)

    rings = []
    for j in range(ARCH_SEGS + 1):
        phi = math.pi * j / ARCH_SEGS
        c, s = math.cos(phi), math.sin(phi)
        rings.append([
            world(ro * c, ro * s, -hd),   # 0 outer-back
            world(ro * c, ro * s, +hd),   # 1 outer-front
            world(ri * c, ri * s, +hd),   # 2 inner-front
            world(ri * c, ri * s, -hd),   # 3 inner-back
        ])
    for j in range(ARCH_SEGS):
        r0, r1 = rings[j], rings[j + 1]
        _add_quad(mesh, r0[0], r0[1], r1[1], r1[0])   # outer arc face
        _add_quad(mesh, r0[1], r0[2], r1[2], r1[1])   # front face
        _add_quad(mesh, r0[2], r0[3], r1[3], r1[2])   # inner arc face
        _add_quad(mesh, r0[3], r0[0], r1[0], r1[3])   # back face
    # foot caps
    f0, f1 = rings[0], rings[-1]
    _add_quad(mesh, f0[0], f0[1], f0[2], f0[3])
    _add_quad(mesh, f1[3], f1[2], f1[1], f1[0])


def _empty_mesh(name, texture):
    return {"name": name, "texture": texture,
            "verts": [], "normals": [], "uvs": [], "indices": []}


def write_arch_meshes():
    # Three running gates (cyan) + the distinct start/finish gate (amber).
    gates = _empty_mesh("race_arch", "race_arch.png")
    for deg in GATE_ANGLES_DEG[:-1]:
        _arch_into(gates, deg)
    sb.write_spm(RACE_DIR / "race_arch.spm", gates)

    finish = _empty_mesh("race_arch_finish", "race_arch_finish.png")
    _arch_into(finish, GATE_ANGLES_DEG[-1])
    sb.write_spm(RACE_DIR / "race_arch_finish.spm", finish)


def write_banner_mesh():
    # A flat banner spanning the track just ahead of the start grid, facing the
    # oncoming karts. It states the lap rule (texture rendered separately as
    # banner_text.png). Baked at world coords so the text orientation is fixed.
    a = math.radians(18.0)
    ux, uz = math.cos(a), math.sin(a)
    cx, cz = VALLEY_R * ux, VALLEY_R * uz
    half_w, y_bot, y_top = 12.0, 5.0, 9.5

    def P(s, y):
        return (cx + s * ux, y, cz + s * uz)

    # normal points back toward the start grid (-tangent) so the karts see the front.
    # UVs are oriented so the text reads left-to-right from the oncoming karts.
    nrm = (math.sin(a), 0.0, -math.cos(a))
    mesh = {
        "name": "race_lap_banner", "texture": "banner_text.png",
        "verts": [P(half_w, y_bot), P(-half_w, y_bot), P(-half_w, y_top), P(half_w, y_top)],
        "normals": [nrm] * 4,
        "uvs": [(1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0)],
        "indices": [0, 1, 2, 0, 2, 3],
    }
    sb.write_spm(RACE_DIR / "race_lap_banner.spm", mesh)


def write_arch_textures():
    def bars(base, accent):
        def px(u, v):
            stripe = ((v * 8.0) % 1.0) < 0.5
            col = accent if stripe else base
            glow = 0.25 * (1.0 - abs(2.0 * u - 1.0))
            return (min(1.0, col[0] + glow), min(1.0, col[1] + glow),
                    min(1.0, col[2] + glow), 1.0)
        return px
    sb.write_png(RACE_DIR / "race_arch.png", 64, 64, bars((0.05, 0.45, 0.85), (0.55, 0.95, 1.0)))
    sb.write_png(RACE_DIR / "race_arch_finish.png", 64, 64,
                 bars((0.85, 0.55, 0.05), (1.0, 0.95, 0.45)))


# ---------------------------------------------------------------------------
# Animated hazards (goldstone boson + Buridan's donkey)
# ---------------------------------------------------------------------------
# These are driven by scene-data animation curves (Blender-IPO style), NOT the
# battle-mode C++ director -- so they run in this lap-based race world too.
# fps=25 (STK default); time = (frame-1)/fps. Curve-animated track objects are
# auto-registered as relativity-exempt (track_object.cpp), so the fast-orbiting
# boson renders on its hitbox instead of smearing through the rim.
FPS = 25.0


def _curve(channel, points, interp="linear"):
    out = [f'    <curve channel="{channel}" interpolation="{interp}" extend="cyclic">']
    out += [f'      <p c="{f:.3f} {v:.3f}"/>' for f, v in points]
    out.append('    </curve>')
    return out


def _hazard_object_lines():
    lines = []

    # --- Goldstone boson: constant-radius orbit of the valley (matches battle:
    #     r=VALLEY_R, y=2.9, omega~0.72 rad/s -> ~8.6 s/rev) + a tumble.
    n = 36
    span = 216.0                       # frames; 216/25 = 8.64 s per orbit
    locx, locz = [], []
    for k in range(n + 1):
        fr = 1.0 + span * k / n
        ang = TAU * k / n
        locx.append((fr, VALLEY_R * math.cos(ang)))
        locz.append((fr, VALLEY_R * math.sin(ang)))
    lines.append('  <object id="sb_goldstone_boson" type="animation"'
                 ' model="goldstone_boson_hazard.spm" xyz="46.000 2.900 0.000"'
                 ' hpr="0 0 0" scale="2.4 2.4 2.4" interaction="explode" explode="y"'
                 ' shape="sphere" radius="2.2" fps="25" skeletal-animation="false">')
    lines += _curve("LocX", locx)
    lines += _curve("LocZ", locz)
    lines += _curve("RotY", [(1.0, 0.0), (76.0, 360.0)])      # spin ~3 s
    lines += _curve("RotX", [(1.0, 0.0), (131.0, 360.0)])     # tumble ~5.2 s
    lines.append('  </object>')

    # --- Buridan's donkey: rolls off the unstable crown out across the valley
    #     and back (fixed +X heading), pausing (indecision) at each end.
    r_keys = [(1, 0), (40, 0), (55, 14), (70, 30), (85, 46), (100, 55),
              (112, 55), (127, 46), (142, 30), (157, 14), (172, 0), (200, 0)]
    dlocx = [(f, r) for f, r in r_keys]
    dlocy = [(f, ring_height(r) + 0.95) for f, r in r_keys]
    lines.append('  <object id="sb_buridan_donkey" type="animation"'
                 ' model="donkey_hazard.spm" xyz="0.000 15.950 0.000"'
                 ' hpr="0 0 0" scale="2.4 2.4 2.4" interaction="explode" explode="y"'
                 ' shape="sphere" radius="2.5" fps="25" skeletal-animation="false">')
    lines += _curve("LocX", dlocx)
    lines += _curve("LocY", dlocy)
    lines += _curve("RotZ", [(1.0, 0.0), (200.0, 1440.0)])    # rolling
    lines.append('  </object>')
    return lines


# ---------------------------------------------------------------------------
# Scene (arena + lights + arches + hazards + checks + start grid + items)
# ---------------------------------------------------------------------------
def write_scene_xml():
    lines = ['<?xml version="1.0"?>', '<scene>',
             '  <sky-color rgb="3 6 13"/>',
             '  <camera far="650"/>',
             '  <sun xyz="-40 110 55" sun-diffuse="220 230 255" sun-specular="255 240 200"'
             ' ambient="110 115 130" fog="true" fog-color="4 8 16" fog-start="70" fog-end="360"/>']
    for x, z, idn in [(46, 0, "vl0"), (23, 39.8, "vl1"), (-23, 39.8, "vl2"),
                      (-46, 0, "vl3"), (-23, -39.8, "vl4"), (23, -39.8, "vl5")]:
        lines.append(f'  <light xyz="{x:.1f} 5.0 {z:.1f}" id="{idn}" distance="60.0"'
                     ' energy="2.0" color="200 210 255"/>')
    lines.append('  <light xyz="0.0 10.0 0.0" id="vl6" distance="40.0" energy="1.5" color="255 220 160"/>')
    lines.append('  <track model="spontaneous_breakdown_arena.spm" x="0" y="0" z="0">')
    lines.append('  </track>')

    # Arches (baked at world positions, placed once at the origin).
    lines.append('  <object id="race_arches" type="animation" model="race_arch.spm"'
                 ' xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"'
                 ' glow="90 200 255" forcedbloom="true" bloompower="1.4" skeletal-animation="false"/>')
    lines.append('  <object id="race_arch_finish" type="animation" model="race_arch_finish.spm"'
                 ' xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"'
                 ' glow="255 200 80" forcedbloom="true" bloompower="1.6" skeletal-animation="false"/>')
    # Rule banner over the start line: all gates must be passed in sequence.
    lines.append('  <object id="race_lap_banner" type="animation" model="race_lap_banner.spm"'
                 ' xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"'
                 ' forcedbloom="true" bloompower="1.2" skeletal-animation="false"/>')

    # Orbiting goldstone boson + Buridan's donkey hazards.
    lines += _hazard_object_lines()

    # Lap gates -- STRICTLY SEQUENTIAL. kind="activate" check-lines default to
    # INACTIVE; only the check-lap is active at reset and arms gate 1. Each gate,
    # when crossed, activates the NEXT and deactivates itself, so a kart must
    # pass 90 -> 180 -> 270 -> 0 in order: skipping one leaves the next (and the
    # lap gate) inactive, so no lap registers until all four are cleared in turn.
    lines.append('  <checks>')
    lines.append('    <check-lap kind="lap" active="true" same-group="0" other-ids="1"/>')
    for i, deg in enumerate(GATE_ANGLES_DEG):
        grp = i + 1
        p1 = ring_pt(CHECK_R_IN, deg)
        p2 = ring_pt(CHECK_R_OUT, deg)
        if grp < len(GATE_ANGLES_DEG):
            lines.append(f'    <check-line kind="activate" same-group="{grp}" other-ids="{grp + 1}"'
                         f' p1="{fmt_vec(p1)}" p2="{fmt_vec(p2)}"/>')
        else:
            lines.append(f'    <check-line kind="lap" active="false" same-group="{grp}" other-ids="1"'
                         f' p1="{fmt_vec(p1)}" p2="{fmt_vec(p2)}"/>')
    lines.append('  </checks>')

    # Start grid: two columns, just past the finish gate, facing CCW.
    for idx in range(10):
        row = idx // 2
        col = idx % 2
        deg = START_ANGLE_DEG + row * 2.2
        r = 43.0 if col == 0 else 49.0
        a = math.radians(deg)
        x, z = math.cos(a) * r, math.sin(a) * r
        y = ring_height(r) + 1.0
        tx, tz = -math.sin(a), math.cos(a)
        heading = math.degrees(math.atan2(tx, tz))
        lines.append(f'  <start position="{idx}" x="{x:.3f}" y="{y:.3f}" z="{z:.3f}" h="{heading:.2f}"/>')

    # --- Item layout (tuned to the banked circular sprint) -------------------
    # Small-nitro boost lanes: a short trail of cans just after each gate on the
    # racing centreline, so passing a gate (which you must, in order) feeds you
    # straight into a boost out of it.
    for g in GATE_ANGLES_DEG:                      # 90, 180, 270, 0
        for d in (6.0, 10.0, 14.0):
            p = ring_pt(VALLEY_R, g + d)
            lines.append(f'  <small-nitro x="{p[0]:.3f}" y="{p[1] + 0.6:.3f}" z="{p[2]:.3f}"/>')

    # Big-nitro tanks: high-value boosts tucked off the ideal line (a tight inside
    # dive or a wide outside line through a mid-arc) -> a risk/reward choice.
    for deg, r in ((45.0, 40.5), (165.0, 51.5), (300.0, 40.5)):
        p = ring_pt(r, deg)
        lines.append(f'  <big-nitro x="{p[0]:.3f}" y="{p[1] + 0.7:.3f}" z="{p[2]:.3f}"/>')

    # Bonus boxes on the centreline between the boost lanes.
    for deg in (135.0, 225.0, 350.0):
        p = ring_pt(VALLEY_R, deg)
        lines.append(f'  <item x="{p[0]:.3f}" y="{p[1] + 0.6:.3f}" z="{p[2]:.3f}"/>')

    # Bananas punishing an over-greedy inside cut.
    for deg in (28.0, 118.0, 208.0, 298.0):
        p = ring_pt(41.0, deg)
        lines.append(f'  <banana x="{p[0]:.3f}" y="{p[1] + 0.4:.3f}" z="{p[2]:.3f}"/>')

    lines.append('</scene>')
    (RACE_DIR / "scene.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_track_xml():
    (RACE_DIR / "track.xml").write_text(
        '<?xml version="1.0"?>\n'
        '<track  name           = "Spontaneous Breakdown Sprint"\n'
        '        version        = "7"\n'
        '        groups         = "standard minkowski"\n'
        '        designer       = "Robson Christie"\n'
        '        music          = "highway_gravel.music"\n'
        '        screenshot     = "spontaneous_breakdown_screenshot.png"\n'
        '        smooth-normals = "false"\n'
        '        default-number-of-laps = "3"\n'
        '        reverse        = "Y">\n'
        '</track>\n',
        encoding="utf-8",
    )


def write_materials_xml():
    (RACE_DIR / "materials.xml").write_text(
        '<?xml version="1.0"?>\n<materials>\n'
        '  <material name="sb_arena_surface.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>\n'
        '  <material name="sb_collision.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>\n'
        '  <material name="race_arch.png" shader="additive" backface-culling="N"/>\n'
        '  <material name="race_arch_finish.png" shader="additive" backface-culling="N"/>\n'
        '  <material name="goldstone_boson_hazard.png" backface-culling="N"/>\n'
        '  <material name="donkey_hazard.png" backface-culling="N"/>\n'
        '  <material name="banner_text.png" shader="additive" backface-culling="N"/>\n'
        '</materials>\n',
        encoding="utf-8",
    )


def copy_shared_assets():
    for fname in ("spontaneous_breakdown_arena.spm", "sb_arena_surface.png",
                  "sb_collision.png", "spontaneous_breakdown_screenshot.png",
                  # GLB-derived hazards reused as moving obstacles in the race.
                  "goldstone_boson_hazard.spm", "goldstone_boson_hazard.png",
                  "donkey_hazard.spm", "donkey_hazard.png"):
        src = SRC_DIR / fname
        if src.exists():
            shutil.copy2(src, RACE_DIR / fname)


def main():
    RACE_DIR.mkdir(parents=True, exist_ok=True)
    copy_shared_assets()
    write_arch_textures()
    write_arch_meshes()
    # NOTE: banner_text.png (the rule-banner texture) is rendered by Blender and
    # is not regenerated here; this only writes the banner geometry that uses it.
    write_banner_mesh()
    write_quads_xml()
    write_graph_xml()
    write_scene_xml()
    write_materials_xml()
    write_track_xml()
    print(f"Generated {RACE_DIR}")


if __name__ == "__main__":
    main()
