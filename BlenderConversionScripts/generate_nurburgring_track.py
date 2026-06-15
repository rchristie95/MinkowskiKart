#!/usr/bin/env python3
"""Generate a game-ready Nuerburgring GP-Strecke track package for MinkowskiKart.

Pure Python (no Blender). Mirrors the lightweight SPM/PNG writers used by
generate_spontaneous_breakdown_track.py and the racing-track XML shape
(quads / graph / checklines / start grid) used by generate_mobius_track.py.

Pipeline:
  1. Fetch the GP-Strecke centerline from OpenStreetMap (Overpass API), cached.
  2. Project lat/lon -> local metres, centred on the loop centroid.
  3. Sample real terrain elevation from a public DEM (OpenTopoData SRTM), cached.
     Falls back to a flat track if the network/DEM is unavailable.
  4. Resample to uniform arc-length spacing and smooth.
  5. Build road / collision / curb / grass meshes (SPM) + procedural textures.
  6. Emit quads.xml, graph.xml, scene.xml, materials.xml, track.xml.

Run:  python BlenderConversionScripts/generate_nurburgring_track.py

Attribution: circuit geometry (c) OpenStreetMap contributors (ODbL); elevation
from OpenTopoData / SRTM. See LICENSE.txt in the generated track folder.
"""

from __future__ import annotations

import json
import math
import random
import struct
import time
import urllib.parse
import urllib.request
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TRACK_DIR = ROOT / "stk-assets" / "tracks" / "nurburgring_gp"
OSM_CACHE = TRACK_DIR / "nurburgring_osm_cache.json"
ELEV_CACHE = TRACK_DIR / "nurburgring_elevation_cache.json"

# Bounding box tight around the GP-Strecke (excludes most of the Nordschleife).
#   south, west, north, east   (WGS84 degrees)
BBOX = (50.3290, 6.9380, 50.3440, 6.9600)
# Seed near the GP start/finish straight; the loop tracer starts from the
# nearest mapped node to this point.
SEED_LATLON = (50.33287, 6.94750)

EARTH_R = 6371000.0
TAU = math.tau

# Track geometry (metres).
ROAD_HALF_WIDTH = 6.0          # 12 m wide racing surface
GRAPH_HALF_WIDTH = 5.2         # driveline quads sit just inside the edges
CURB_WIDTH = 0.7
# Apron half-extent beyond the road. Kept modest so the swept ribbon does not
# self-intersect on the tight Mercedes-Arena hairpins (corner radius ~15 m).
GRASS_WIDTH = 8.0
# The grass apron's outer edge slopes down by this much to meet the terrain
# (which is carved this far below the road in the corridor) -- removes both the
# terrain-poking-through-the-track and the apron-edge cliff.
GRASS_OUTER_DROP = 1.6
RESAMPLE_SPACING = 8.0         # ~640 samples over 5.1 km
SMOOTH_PASSES = 2
SMOOTH_WINDOW = 2             # +/- samples averaged per smoothing pass
UV_TILE_LENGTH = 12.0         # metres of road per asphalt texture tile

# Loop length sanity window for picking the GP loop out of the OSM data.
LOOP_MIN_M = 3000.0
LOOP_MAX_M = 6800.0

# Start grid.
START_ROWS = 4
START_COLS = 3
START_LATERALS = (-3.2, 0.0, 3.2)
START_ROW_SPACING_M = 8.0
START_LIFT = 0.6
START_MIN_DISTANCE = 2.4

# Trackside scenery: pine forest built from shared stk-assets/library objects
# (same approach as the black_forest track). No assets are copied -- the engine
# resolves <library name="..."> from stk-assets/library/ at load time.
TREE_LONG_STRIDE = 1          # place a cluster at (almost) every centerline sample
TREES_PER_SIDE = 7            # trees per side per sample -> dense forest (~20x)
TREE_SPACING_M = 8.0          # along-track jitter magnitude
TREE_MARGIN_M = 4.0           # gap beyond the grass apron before trees start
FOREST_DEPTH_M = 95.0         # trees scatter from the verge back this far
TREE_SEED = 20260615
# Reject any tree/undergrowth closer than this to ANY centerline point -- stops
# trees landing on a different part of the circuit where the loop runs near
# itself (road is +/-6 m, apron to +/-14 m).
TREE_TRACK_CLEARANCE = 14.0
# Conifers dominate (Eifel/Black-Forest look); a few broadleaf for variety.
# stklib_pinetree_b/a have physics collision -> keep them out past the runoff;
# stklib_lowPineTree_a and the small props are collision-free (ghost).
TREE_TYPES = (
    ("stklib_pinetree_b", 5),
    ("stklib_pinetree_a", 3),
    ("stklib_pinetree_c", 2),
    ("stklib_lowPineTree_a", 3),
    ("stklib_autumnBirch_a", 1),
    ("stklib_autumnTree_c", 1),
    ("stklib_autumnTree_d", 1),
)
UNDERGROWTH_TYPES = ("stklib_fern_a", "stklib_treeStump_a", "stklib_autumnSmallBush_a")

# Rolling terrain built from the same SRTM DEM so trees sit on real ground
# instead of floating over the void beyond the narrow grass apron. The grid is
# carved flat to the road height in a corridor around the track so the road
# always sits just above it, then blends out to real elevation.
TERRAIN_MARGIN_M = 150.0       # how far terrain extends beyond the track bbox
TERRAIN_RES_M = 16.0           # grid spacing (finer = better road conform)
TERRAIN_CARVE_INNER = 15.0     # within this dist of the road, terrain = road height
TERRAIN_CARVE_OUTER = 55.0     # beyond this, pure DEM elevation
TERRAIN_ROAD_DROP = 1.6        # terrain sits this far below the road in the corridor
TERRAIN_FILE = "nurburgring_terrain.spm"

# Ground zipper (speed-boost) pads on the straights. Detected by STK via the
# GFX-effect mesh, so they are placed as ghost-texture lifted just above the
# road (matches the mobius track).
ZIPPER_STRAIGHT_DOT = 0.9994   # consecutive tangents this aligned == "straight"
ZIPPER_MIN_RUN = 8             # min straight length (samples) to host pads
ZIPPER_PAD_STRIDE = 8          # samples between pads along a straight
ZIPPER_HALF_WIDTH = 2.6        # pad half-width (narrower than the road)
ZIPPER_LIFT = 0.09            # height above the road surface (<0.35 to register)


# --------------------------------------------------------------------------- #
# Vector helpers (kept self-contained, matching the existing generators).
# --------------------------------------------------------------------------- #
def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


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


# --------------------------------------------------------------------------- #
# SPM / PNG writers (identical format to the existing generators).
# --------------------------------------------------------------------------- #
def pack_normal_1010102(n):
    packed = 0
    for shift, value in ((0, n[0]), (10, n[1]), (20, n[2])):
        value = max(-1.0, min(1.0, value))
        part = int(value * 511.0 + 0.5) if value > 0.0 else int(value * 512.0 - 0.5)
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


# --------------------------------------------------------------------------- #
# OSM centerline fetch + loop extraction.
# --------------------------------------------------------------------------- #
OVERPASS_ENDPOINTS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]


def fetch_osm():
    if OSM_CACHE.exists():
        print(f"[osm] using cached {OSM_CACHE.name}")
        return json.loads(OSM_CACHE.read_text(encoding="utf-8"))
    s, w, n, e = BBOX
    query = (
        "[out:json][timeout:90];"
        f'(way["highway"="raceway"]({s},{w},{n},{e}););'
        "(._;>;);out body;"
    )
    data = urllib.parse.urlencode({"data": query}).encode("utf-8")
    last_err = None
    for endpoint in OVERPASS_ENDPOINTS:
        try:
            print(f"[osm] querying {endpoint}")
            req = urllib.request.Request(endpoint, data=data,
                                         headers={"User-Agent": "MinkowskiKart-track-gen"})
            with urllib.request.urlopen(req, timeout=120) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
            TRACK_DIR.mkdir(parents=True, exist_ok=True)
            OSM_CACHE.write_text(json.dumps(payload), encoding="utf-8")
            print(f"[osm] cached -> {OSM_CACHE.name}")
            return payload
        except Exception as exc:  # noqa: BLE001
            last_err = exc
            print(f"[osm] failed: {exc}")
    raise RuntimeError(f"Overpass fetch failed: {last_err}")


def project(lat, lon, lat0, lon0):
    x = math.radians(lon - lon0) * math.cos(math.radians(lat0)) * EARTH_R
    z = math.radians(lat - lat0) * EARTH_R
    return (x, z)


# Way names dropped before tracing: pit lanes, Nordschleife/Mullenbach links,
# and race variants. What remains is the GP/Sprint racing line plus Nordschleife
# stubs that dead-end at the bbox edge (removed by dead-end pruning).
EXCLUDE_NAME_SUBSTR = ("Boxengasse", "Anbindung", "Variante")


def extract_loop(osm):
    """Extract the circuit as an ordered list of (lat, lon).

    Build the raceway graph (minus pit/links/variants), prune dead-end stubs
    (the clipped Nordschleife arcs), contract the remainder into arcs between
    junction hubs, then pick the longest closed cycle. The loop is rotated so
    it starts on the node nearest the real start/finish straight (SEED_LATLON).
    """
    nodes = {el["id"]: (el["lat"], el["lon"])
             for el in osm["elements"] if el["type"] == "node"}
    adj = {}
    for el in osm["elements"]:
        if el["type"] != "way":
            continue
        if any(s in el.get("tags", {}).get("name", "") for s in EXCLUDE_NAME_SUBSTR):
            continue
        nd = [n for n in el.get("nodes", []) if n in nodes]
        for a, b in zip(nd, nd[1:]):
            adj.setdefault(a, set()).add(b)
            adj.setdefault(b, set()).add(a)
    if not adj:
        raise RuntimeError("No raceway ways found in bbox; widen BBOX.")

    # Prune dead ends (clipped Nordschleife stubs, pit remnants).
    changed = True
    while changed:
        changed = False
        for nid in list(adj):
            if len(adj[nid]) <= 1:
                for m in list(adj[nid]):
                    adj[m].discard(nid)
                del adj[nid]
                changed = True
    if not adj:
        raise RuntimeError("Pruning removed everything; loosen EXCLUDE filters.")

    lat0 = (BBOX[0] + BBOX[2]) / 2.0
    lon0 = (BBOX[1] + BBOX[3]) / 2.0
    pos = {nid: project(la, lo, lat0, lon0) for nid, (la, lo) in nodes.items()}

    def dist(a, b):
        return math.hypot(pos[a][0] - pos[b][0], pos[a][1] - pos[b][1])

    hubs = {nid for nid in adj if len(adj[nid]) != 2}

    # Pure cycle (no junctions): walk it directly.
    if not hubs:
        start = next(iter(adj))
        order = [start]
        prev, cur = None, start
        while True:
            nxt = [x for x in adj[cur] if x != prev]
            if not nxt:
                break
            prev, cur = cur, nxt[0]
            if cur == start:
                break
            order.append(cur)
        total = sum(dist(order[i], order[(i + 1) % len(order)]) for i in range(len(order)))
        print(f"[osm] loop (single cycle): {len(order)} nodes, {total:.0f} m")
        return _rotate_to_seed([nodes[n] for n in order])

    # Contract chains of degree-2 nodes into arcs between hubs.
    arcs = []          # (hub_a, hub_b, length, [node ids inclusive])
    seen = set()
    for h in hubs:
        for first in adj[h]:
            ek = (min(h, first), max(h, first))
            if ek in seen:
                continue
            path = [h, first]
            seen.add(ek)
            prev, cur = h, first
            length = dist(h, first)
            while cur not in hubs:
                nxts = [x for x in adj[cur] if x != prev]
                if len(nxts) != 1:
                    break
                seen.add((min(cur, nxts[0]), max(cur, nxts[0])))
                length += dist(cur, nxts[0])
                path.append(nxts[0])
                prev, cur = cur, nxts[0]
            if path[-1] in hubs and path[0] != path[-1]:
                arcs.append((path[0], path[-1], length, path))

    graph = {}
    for i, (a, b, _l, _p) in enumerate(arcs):
        graph.setdefault(a, []).append((b, i))
        graph.setdefault(b, []).append((a, i))

    best = {"len": 0.0, "edges": None}

    def dfs(start, cur, used, length, seq):
        for nb, ei in graph.get(cur, ()):
            if ei in used:
                continue
            new_len = length + arcs[ei][2]
            if nb == start and len(seq) >= 1 and new_len > best["len"]:
                best["len"] = new_len
                best["edges"] = seq + [ei]
            elif nb != start:
                dfs(start, nb, used | {ei}, new_len, seq + [ei])

    for h in hubs:
        dfs(h, h, set(), 0.0, [])
    if not best["edges"]:
        raise RuntimeError("No closed cycle found; inspect the OSM cache.")

    # Reconstruct the ordered node loop from the chosen arc sequence.
    order = []
    cursor = arcs[best["edges"][0]][0]
    for ei in best["edges"]:
        a, b, _l, path = arcs[ei]
        seg = path if a == cursor else list(reversed(path))
        order.extend(seg[:-1])      # drop shared hub (added by next arc)
        cursor = seg[-1]
    print(f"[osm] loop (longest cycle): {len(order)} nodes, {best['len']:.0f} m")
    if not (LOOP_MIN_M <= best["len"] <= LOOP_MAX_M):
        print(f"[osm] WARNING: loop length {best['len']:.0f} m outside expected "
              f"{LOOP_MIN_M:.0f}-{LOOP_MAX_M:.0f} m window.")
    return _rotate_to_seed([nodes[n] for n in order])


def _rotate_to_seed(loop_latlon):
    """Rotate the closed loop so it begins at the point nearest SEED_LATLON."""
    si = min(range(len(loop_latlon)),
             key=lambda i: (loop_latlon[i][0] - SEED_LATLON[0]) ** 2
             + (loop_latlon[i][1] - SEED_LATLON[1]) ** 2)
    return loop_latlon[si:] + loop_latlon[:si]


# --------------------------------------------------------------------------- #
# Elevation sampling (DEM), cached. Flat fallback on failure.
# --------------------------------------------------------------------------- #
def fetch_elevations(latlons):
    cache = {}
    if ELEV_CACHE.exists():
        cache = json.loads(ELEV_CACHE.read_text(encoding="utf-8"))

    def key(la, lo):
        return f"{la:.6f},{lo:.6f}"

    missing = [(la, lo) for la, lo in latlons if key(la, lo) not in cache]
    if missing:
        print(f"[dem] fetching {len(missing)} elevation samples")
        try:
            for i in range(0, len(missing), 100):
                batch = missing[i:i + 100]
                locs = "|".join(f"{la:.6f},{lo:.6f}" for la, lo in batch)
                url = "https://api.opentopodata.org/v1/srtm30m?locations=" + urllib.parse.quote(locs)
                req = urllib.request.Request(url, headers={"User-Agent": "MinkowskiKart-track-gen"})
                with urllib.request.urlopen(req, timeout=60) as resp:
                    res = json.loads(resp.read().decode("utf-8"))
                for (la, lo), r in zip(batch, res["results"]):
                    if r.get("elevation") is None:
                        raise RuntimeError("null elevation in DEM response")
                    cache[key(la, lo)] = float(r["elevation"])
                time.sleep(1.2)  # respect OpenTopoData rate limit
            TRACK_DIR.mkdir(parents=True, exist_ok=True)
            ELEV_CACHE.write_text(json.dumps(cache), encoding="utf-8")
            print(f"[dem] cached -> {ELEV_CACHE.name}")
        except Exception as exc:  # noqa: BLE001
            print(f"[dem] elevation fetch failed ({exc}); FLAT FALLBACK")
            return None
    return [cache[key(la, lo)] for la, lo in latlons]


# --------------------------------------------------------------------------- #
# Centerline resampling / smoothing / frame computation.
# --------------------------------------------------------------------------- #
def resample_loop(points2d, spacing):
    """points2d: closed list of (x,z). Returns uniform-spacing (x,z) list."""
    pts = list(points2d)
    if pts[0] != pts[-1]:
        pts.append(pts[0])
    seglen = [math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
              for i in range(len(pts) - 1)]
    total = sum(seglen)
    n = max(16, int(round(total / spacing)))
    step = total / n
    out, target, acc, si = [], 0.0, 0.0, 0
    for _ in range(n):
        while si < len(seglen) and acc + seglen[si] < target:
            acc += seglen[si]
            si += 1
        if si >= len(seglen):
            break
        t = (target - acc) / (seglen[si] or 1.0)
        ax, az = pts[si]
        bx, bz = pts[si + 1]
        out.append((ax + (bx - ax) * t, az + (bz - az) * t))
        target += step
    return out


def smooth_closed(values, window, passes):
    arr = list(values)
    n = len(arr)
    for _ in range(passes):
        nxt = []
        for i in range(n):
            acc = 0.0
            for k in range(-window, window + 1):
                acc += arr[(i + k) % n]
            nxt.append(acc / (2 * window + 1))
        arr = nxt
    return arr


def build_samples(loop_xz, elevations):
    n = len(loop_xz)
    xs = smooth_closed([p[0] for p in loop_xz], SMOOTH_WINDOW, SMOOTH_PASSES)
    zs = smooth_closed([p[1] for p in loop_xz], SMOOTH_WINDOW, SMOOTH_PASSES)
    if elevations is None:
        ys = [0.0] * n
    else:
        base = min(elevations)
        ys = smooth_closed([e - base for e in elevations], SMOOTH_WINDOW, SMOOTH_PASSES)
    centers = [(xs[i], ys[i], zs[i]) for i in range(n)]

    samples = []
    cum = 0.0
    for i in range(n):
        c = centers[i]
        nxt = centers[(i + 1) % n]
        prv = centers[(i - 1) % n]
        t3d = vnorm(vsub(nxt, prv), (0.0, 0.0, 1.0))
        th = vnorm((t3d[0], 0.0, t3d[2]), (0.0, 0.0, 1.0))   # horizontal heading
        lat = vnorm((th[2], 0.0, -th[0]), (1.0, 0.0, 0.0))   # right-hand lateral
        nrm = vnorm(vcross(t3d, lat), (0.0, 1.0, 0.0))
        samples.append({"c": c, "t": th, "lat": lat, "n": nrm, "s": cum})
        cum += vlength(vsub(nxt, c))
    return samples


def edge(sample, offset, lift=0.0):
    return vadd(vadd(sample["c"], vmul(sample["lat"], offset)),
                vmul(sample["n"], lift))


# --------------------------------------------------------------------------- #
# Mesh builders (ribbons swept along the centerline).
# --------------------------------------------------------------------------- #
def build_ribbon(name, texture, samples, off_left, off_right, dy=0.0,
                 dy_l=None, dy_r=None, u0=0.0, u1=1.0):
    if dy_l is None:
        dy_l = dy
    if dy_r is None:
        dy_r = dy
    verts, normals, uvs, indices = [], [], [], []
    n = len(samples)
    for sm in samples:
        verts.append(edge(sm, off_left, dy_l))
        verts.append(edge(sm, off_right, dy_r))
        normals.append(sm["n"])
        normals.append(sm["n"])
        v = sm["s"] / UV_TILE_LENGTH
        uvs.append((u0, v))
        uvs.append((u1, v))
    for i in range(n):
        a = 2 * i              # left_i      (a+1 = right_i)
        b = 2 * ((i + 1) % n)  # left_next   (b+1 = right_next)
        # Wind CCW so face normals point up (matches the up-facing vertex
        # normals); the reversed winding produced down-facing faces which
        # break the SP/Vulkan depth + shadow passes.
        indices.extend([a, b, b + 1, a, b + 1, a + 1])
    return {"name": name, "texture": texture, "verts": verts,
            "normals": normals, "uvs": uvs, "indices": indices}


def build_meshes(samples):
    meshes = {}
    # Visual asphalt road (root <track> model).
    meshes["road_visual"] = build_ribbon(
        "Nurburgring_Road_Visual", "nurburgring_asphalt.png",
        samples, -ROAD_HALF_WIDTH, ROAD_HALF_WIDTH)
    # Collision split into road (grippy) + grass apron (slows karts). Both flat
    # at road level so the runoff is drivable; the grass material carries the
    # slowdown so going off the asphalt costs you speed.
    meshes["road_collision"] = build_ribbon(
        "Nurburgring_Road_Collision", "nurburgring_collision.png",
        samples, -ROAD_HALF_WIDTH, ROAD_HALF_WIDTH, dy=-0.02)
    meshes["grass_col_left"] = build_ribbon(
        "Nurburgring_Grass_Col_Left", "nurburgring_grass.png",
        samples, -(ROAD_HALF_WIDTH + GRASS_WIDTH), -ROAD_HALF_WIDTH, dy=-0.02)
    meshes["grass_col_right"] = build_ribbon(
        "Nurburgring_Grass_Col_Right", "nurburgring_grass.png",
        samples, ROAD_HALF_WIDTH, (ROAD_HALF_WIDTH + GRASS_WIDTH), dy=-0.02)
    # Grass apron, two strips (visual). Inner edge at the road lip, outer edge
    # sloped down to meet the terrain so there is no cliff / no terrain poking
    # over the track. (off_left is the OUTER edge for the left strip.)
    meshes["grass_left"] = build_ribbon(
        "Nurburgring_Grass_Left", "nurburgring_grass.png",
        samples, -(ROAD_HALF_WIDTH + GRASS_WIDTH), -ROAD_HALF_WIDTH,
        dy_l=-GRASS_OUTER_DROP, dy_r=-0.05,
        u0=0.0, u1=GRASS_WIDTH / UV_TILE_LENGTH)
    meshes["grass_right"] = build_ribbon(
        "Nurburgring_Grass_Right", "nurburgring_grass.png",
        samples, ROAD_HALF_WIDTH, (ROAD_HALF_WIDTH + GRASS_WIDTH),
        dy_l=-0.05, dy_r=-GRASS_OUTER_DROP,
        u0=0.0, u1=GRASS_WIDTH / UV_TILE_LENGTH)
    # Ground zipper boost pads on the straights.
    meshes["zippers"] = make_zipper_mesh(samples)
    # Red/white curbs hugging the road edges (visual).
    meshes["curb_left"] = build_ribbon(
        "Nurburgring_Curb_Left", "nurburgring_curb.png",
        samples, -(ROAD_HALF_WIDTH + CURB_WIDTH), -ROAD_HALF_WIDTH, dy=0.02)
    meshes["curb_right"] = build_ribbon(
        "Nurburgring_Curb_Right", "nurburgring_curb.png",
        samples, ROAD_HALF_WIDTH, (ROAD_HALF_WIDTH + CURB_WIDTH), dy=0.02)
    return meshes


MESH_FILES = {
    "road_visual": "nurburgring_road_visual.spm",
    "road_collision": "nurburgring_road_collision.spm",
    "grass_col_left": "nurburgring_grass_col_left.spm",
    "grass_col_right": "nurburgring_grass_col_right.spm",
    "grass_left": "nurburgring_grass_left.spm",
    "grass_right": "nurburgring_grass_right.spm",
    "curb_left": "nurburgring_curb_left.spm",
    "curb_right": "nurburgring_curb_right.spm",
    "zippers": "nurburgring_zippers.spm",
}


def make_zipper_mesh(samples):
    """Lay flat boost pads (zipper material) along the straights, lifted just
    above the road so STK's GFX-mesh zipper probe registers them."""
    n = len(samples)
    is_straight = [vdot(samples[i]["t"], samples[(i + 1) % n]["t"])
                   > ZIPPER_STRAIGHT_DOT for i in range(n)]
    centers = []
    i = 0
    while i < n:
        if not is_straight[i]:
            i += 1
            continue
        j = i
        while j < n and is_straight[j]:
            j += 1
        if j - i >= ZIPPER_MIN_RUN:
            k = i + 2
            while k <= j - 2:
                centers.append(k)
                k += ZIPPER_PAD_STRIDE
        i = j
    if not centers:                      # guarantee at least one pad
        centers.append(2)

    verts, normals, uvs, indices = [], [], [], []
    for ci in centers:
        base = len(verts)
        for col, si in enumerate((ci - 1, ci, ci + 1)):
            sm = samples[si % n]
            u = col / 2.0
            verts.append(edge(sm, -ZIPPER_HALF_WIDTH, ZIPPER_LIFT))
            normals.append(sm["n"])
            uvs.append((u, 0.0))
            verts.append(edge(sm, ZIPPER_HALF_WIDTH, ZIPPER_LIFT))
            normals.append(sm["n"])
            uvs.append((u, 1.0))
        for col in range(2):
            a = base + col * 2
            b = base + (col + 1) * 2
            indices.extend([a, b, b + 1, a, b + 1, a + 1])   # up-facing
    print(f"[zippers] {len(centers)} boost pads on straights")
    return {"name": "Nurburgring_Zippers", "texture": "nurburgring_zipper.png",
            "verts": verts, "normals": normals, "uvs": uvs, "indices": indices}


def build_terrain(samples, base, lat0, lon0):
    """Build a rolling ground grid from the SRTM DEM, carved to the road height
    in a corridor around the circuit so the road sits just above it and blends
    out to real Eifel elevation. Returns (mesh, height_at) where height_at(x,z)
    bilinearly samples the surface (used to ground the trackside trees).
    Visual-only (ghost) so it never feeds the physics mesh (keeps the Vulkan
    collision path clean -- see materials note)."""
    xs = [s["c"][0] for s in samples]
    zs = [s["c"][2] for s in samples]
    minx, maxx = min(xs) - TERRAIN_MARGIN_M, max(xs) + TERRAIN_MARGIN_M
    minz, maxz = min(zs) - TERRAIN_MARGIN_M, max(zs) + TERRAIN_MARGIN_M
    nx = max(2, int((maxx - minx) / TERRAIN_RES_M) + 1)
    nz = max(2, int((maxz - minz) / TERRAIN_RES_M) + 1)
    gx = [minx + (maxx - minx) * i / (nx - 1) for i in range(nx)]
    gz = [minz + (maxz - minz) * j / (nz - 1) for j in range(nz)]

    latlon = []
    for z in gz:
        for x in gx:
            latlon.append((lat0 + math.degrees(z / EARTH_R),
                           lon0 + math.degrees(x / (EARTH_R * math.cos(math.radians(lat0))))))
    dem = fetch_elevations(latlon)

    sample_xz = [(s["c"][0], s["c"][2], s["c"][1]) for s in samples]
    inner2 = TERRAIN_CARVE_INNER ** 2
    heights = [[0.0] * nx for _ in range(nz)]
    k = 0
    for j in range(nz):
        for i in range(nx):
            x, z = gx[i], gz[j]
            bd, rh = 1.0e18, 0.0
            for sx, sz, sy in sample_xz:
                d2 = (x - sx) ** 2 + (z - sz) ** 2
                if d2 < bd:
                    bd, rh = d2, sy
                    if d2 < inner2:
                        break
            d = math.sqrt(bd)
            demh = (dem[k] - base) if dem is not None else rh
            if d <= TERRAIN_CARVE_INNER:
                h = rh - TERRAIN_ROAD_DROP
            elif d >= TERRAIN_CARVE_OUTER:
                h = demh
            else:
                t = (d - TERRAIN_CARVE_INNER) / (TERRAIN_CARVE_OUTER - TERRAIN_CARVE_INNER)
                t = t * t * (3.0 - 2.0 * t)   # smoothstep
                h = (rh - TERRAIN_ROAD_DROP) * (1.0 - t) + demh * t
            heights[j][i] = h
            k += 1

    def vid(i, j):
        return j * nx + i

    verts, normals, uvs, indices = [], [], [], []
    for j in range(nz):
        for i in range(nx):
            verts.append((gx[i], heights[j][i], gz[j]))
            uvs.append((gx[i] / 14.0, gz[j] / 14.0))
            hl = heights[j][max(0, i - 1)]
            hr = heights[j][min(nx - 1, i + 1)]
            hd = heights[max(0, j - 1)][i]
            hu = heights[min(nz - 1, j + 1)][i]
            normals.append(vnorm((hl - hr, 2.0 * TERRAIN_RES_M, hd - hu)))
    for j in range(nz - 1):
        for i in range(nx - 1):
            a, b = vid(i, j), vid(i + 1, j)
            c, d = vid(i + 1, j + 1), vid(i, j + 1)
            indices.extend([a, d, c, a, c, b])   # CCW, up-facing

    def height_at(x, z):
        fx = (x - minx) / (maxx - minx) * (nx - 1)
        fz = (z - minz) / (maxz - minz) * (nz - 1)
        i = max(0, min(nx - 2, int(fx)))
        j = max(0, min(nz - 2, int(fz)))
        tx = max(0.0, min(1.0, fx - i))
        tz = max(0.0, min(1.0, fz - j))
        h0 = heights[j][i] * (1 - tx) + heights[j][i + 1] * tx
        h1 = heights[j + 1][i] * (1 - tx) + heights[j + 1][i + 1] * tx
        return h0 * (1 - tz) + h1 * tz

    mesh = {"name": "Nurburgring_Terrain", "texture": "nurburgring_grass.png",
            "verts": verts, "normals": normals, "uvs": uvs, "indices": indices}
    print(f"[terrain] {nx}x{nz} grid, {len(verts)} verts, "
          f"DEM={'real' if dem is not None else 'flat fallback'}")
    return mesh, height_at


# --------------------------------------------------------------------------- #
# Map landmarks from the Nurburgring GP overview ("gui cartoon"): buildings,
# medical centre, water, parking, access roads. Procedural, visual-only (ghost).
# --------------------------------------------------------------------------- #
def _box_mesh(name, texture, center, size, heading_deg, uvrep=(1.0, 1.0)):
    cx, cy, cz = center            # cy = base (ground) height
    w, h, d = size
    a = math.radians(heading_deg)
    ca, sa = math.cos(a), math.sin(a)

    def rot(lx, lz):
        return (cx + lx * ca - lz * sa, cz + lx * sa + lz * ca)

    hw, hd = w / 2.0, d / 2.0
    bxz = [rot(-hw, -hd), rot(hw, -hd), rot(hw, hd), rot(-hw, hd)]
    bot = [(x, cy, z) for (x, z) in bxz]
    top = [(x, cy + h, z) for (x, z) in bxz]
    verts, normals, uvs, indices = [], [], [], []

    def quad(p0, p1, p2, p3, ur, vr):
        nrm = vnorm(vcross(vsub(p1, p0), vsub(p2, p0)))
        b = len(verts)
        verts.extend([p0, p1, p2, p3])
        normals.extend([nrm] * 4)
        uvs.extend([(0, 0), (ur, 0), (ur, vr), (0, vr)])
        indices.extend([b, b + 1, b + 2, b, b + 2, b + 3])

    uw, ud = uvrep
    quad(bot[0], bot[1], top[1], top[0], w * uw / 4.0, h / 3.0)
    quad(bot[1], bot[2], top[2], top[1], d * ud / 4.0, h / 3.0)
    quad(bot[2], bot[3], top[3], top[2], w * uw / 4.0, h / 3.0)
    quad(bot[3], bot[0], top[0], top[3], d * ud / 4.0, h / 3.0)
    quad(top[0], top[1], top[2], top[3], 1.0, 1.0)   # flat roof
    return {"name": name, "texture": texture, "verts": verts,
            "normals": normals, "uvs": uvs, "indices": indices}


def _flat_patch_mesh(name, texture, center_xz, w, d, heading_deg, lift,
                     height_at, uvrep=4.0, level=False):
    a = math.radians(heading_deg)
    ca, sa = math.cos(a), math.sin(a)
    cx, cz = center_xz
    hw, hd = w / 2.0, d / 2.0
    corners = [(-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd)]
    world = [(cx + lx * ca - lz * sa, cz + lx * sa + lz * ca) for (lx, lz) in corners]
    # level=True -> single flat height (e.g. a lake surface) sunk into the basin.
    flat_y = min(height_at(x, z) for (x, z) in world) + lift if level else None
    verts, normals, uvs, indices = [], [], [], []
    uvc = [(0, 0), (w / uvrep, 0), (w / uvrep, d / uvrep), (0, d / uvrep)]
    for k, (x, z) in enumerate(world):
        y = flat_y if level else height_at(x, z) + lift
        verts.append((x, y, z))
        normals.append((0.0, 1.0, 0.0))
        uvs.append(uvc[k])
    indices.extend([0, 3, 2, 0, 2, 1])   # up-facing
    return {"name": name, "texture": texture, "verts": verts,
            "normals": normals, "uvs": uvs, "indices": indices}


def build_landmarks(samples, height_at):
    """Place the GP-map landmarks (buildings, medical centre, water, parking,
    access roads). Returns (meshes_by_filename, scene_lines)."""
    n = len(samples)
    cx = sum(s["c"][0] for s in samples) / n
    cz = sum(s["c"][2] for s in samples) / n

    def udir(fr, to):
        dx, dz = to[0] - fr[0], to[1] - fr[1]
        L = math.hypot(dx, dz) or 1.0
        return (dx / L, dz / L)

    meshes, lines = {}, ["  <!-- Map landmarks (buildings/medical/water/parking) -->"]

    def add(fname, mesh, interaction="ghost"):
        meshes[fname] = mesh
        lines.append(f'    <static-object model="{fname}" xyz="0 0 0" '
                     f'hpr="0 0 0" scale="1 1 1" interaction="{interaction}"/>')

    def place_xz(si, dist_into):
        """Point dist_into metres toward (>0) / away from (<0) the infield."""
        sx, sz = samples[si]["c"][0], samples[si]["c"][2]
        ix, iz = udir((sx, sz), (cx, cz))
        return (sx + ix * dist_into, sz + iz * dist_into)

    def heading(si):
        t = samples[si]["t"]
        return math.degrees(math.atan2(t[0], t[2]))

    # --- Pit building & paddock (infield, along the start/finish straight) ---
    px, pz = place_xz(0, 60.0)
    add("nbr_pit_building.spm",
        _box_mesh("nbr_pit", "nbr_building.png",
                  (px, height_at(px, pz), pz), (72.0, 7.0, 11.0), heading(0),
                  uvrep=(1.0, 1.0)))
    for k, off in enumerate(((-26, 22), (4, 26), (30, 20))):
        bx, bz = place_xz(0, 60.0 + off[1])
        a = math.radians(heading(0))
        # shift sideways along the straight tangent
        bx += samples[0]["t"][0] * off[0]
        bz += samples[0]["t"][2] * off[0]
        add(f"nbr_paddock_{k}.spm",
            _box_mesh(f"nbr_paddock_{k}", "nbr_building.png",
                      (bx, height_at(bx, bz), bz), (14.0, 5.0, 12.0), heading(0)))

    # --- Medical centre (infield, near track centre) ---
    mx, mz = place_xz(n // 4, 70.0)
    add("nbr_medical.spm",
        _box_mesh("nbr_medical", "nbr_medical.png",
                  (mx, height_at(mx, mz), mz), (12.0, 5.0, 12.0), heading(n // 4)))

    # --- A couple more grandstand-ish buildings on the infield elsewhere ---
    for k, frac in enumerate((0.55, 0.78)):
        si = int(n * frac)
        gx, gz = place_xz(si, 64.0)
        add(f"nbr_building_{k}.spm",
            _box_mesh(f"nbr_building_{k}", "nbr_building.png",
                      (gx, height_at(gx, gz), gz), (30.0, 6.0, 10.0), heading(si)))

    # --- Parking lots (outfield) + short access roads back to the verge ---
    for k, frac in enumerate((0.12, 0.40, 0.64, 0.88)):
        si = int(n * frac)
        lotx, lotz = place_xz(si, -58.0)         # outward
        add(f"nbr_parking_{k}.spm",
            _flat_patch_mesh(f"nbr_parking_{k}", "nbr_parking.png",
                             (lotx, lotz), 38.0, 26.0, heading(si), 0.06,
                             height_at, uvrep=9.0))
        roadx, roadz = place_xz(si, -32.0)       # between lot and track edge
        add(f"nbr_access_{k}.spm",
            _flat_patch_mesh(f"nbr_access_{k}", "nbr_access.png",
                             (roadx, roadz), 6.0, 44.0, heading(si) + 90.0, 0.05,
                             height_at, uvrep=6.0))

    # --- Lake (outfield) ---
    lx, lz = place_xz(n // 2, -100.0)
    add("nbr_water.spm",
        _flat_patch_mesh("nbr_water", "nbr_water.png",
                         (lx, lz), 78.0, 48.0, heading(n // 2), -0.25,
                         height_at, uvrep=10.0, level=True))

    print(f"[landmarks] placed {len(meshes)} buildings/lots/water")
    return meshes, lines


# --------------------------------------------------------------------------- #
# Textures.
# --------------------------------------------------------------------------- #
def asphalt_px(u, v):
    grain = (math.sin(u * 311.0) * math.cos(v * 271.0)) * 0.5 + 0.5
    g = 0.30 + 0.06 * grain
    # Faint lane seam down the middle.
    if abs(u - 0.5) < 0.012:
        g = 0.40
    return (g, g, g * 1.05, 1.0)


def curb_px(u, v):
    band = int(v * 8.0) % 2
    return ((0.85, 0.12, 0.10, 1.0) if band == 0 else (0.92, 0.92, 0.92, 1.0))


def _hash2(ix, iy):
    h = (ix * 374761393 + iy * 668265263) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def grass_px(u, v):
    # Multi-scale lawn: tonal patches + per-cell blade speckle + dirt patches.
    px, py = int(u * 48.0), int(v * 48.0)
    blade = _hash2(px, py)
    patch = 0.5 + 0.5 * math.sin(u * 9.0) * math.cos(v * 7.0)
    c = mix((0.09, 0.24, 0.08), (0.23, 0.46, 0.16), 0.45 * patch + 0.40 * blade)
    if _hash2(px // 6, py // 6) > 0.93:                 # dry dirt patch
        c = mix(c, (0.36, 0.29, 0.17), 0.55)
    if blade > 0.87:                                    # bright blade tips
        c = mix(c, (0.32, 0.58, 0.22), 0.55)
    elif blade < 0.10:                                  # dark shadowed blades
        c = mix(c, (0.05, 0.16, 0.05), 0.5)
    return (c[0], c[1], c[2], 1.0)


def zipper_px(u, v):
    # Forward chevrons. Arrows reversed (now point with the direction of travel;
    # the boost itself was already correct).
    band = ((1.0 - u) * 3.0) % 1.0
    chev = abs(v - 0.5) * 2.0
    arrow = 1.0 if abs(band - chev) < 0.16 else 0.0
    rim = 0.55 if (v < 0.12 or v > 0.88) else 0.0
    glow = max(arrow, rim)
    c = mix((0.02, 0.10, 0.22), (0.25, 0.95, 1.0), glow)
    return (c[0], c[1], c[2], 1.0)


def building_px(u, v):
    # Grid of windows on a wall band; concrete between.
    wall = (0.62, 0.63, 0.66)
    win_x = (u * 6.0) % 1.0
    win_y = (v * 6.0) % 1.0
    lit = 0.25 < win_x < 0.75 and 0.25 < win_y < 0.78
    if lit:
        glow = 0.4 + 0.5 * ((u * 53.0 + v * 31.0) % 1.0)
        c = (0.20 + 0.25 * glow, 0.30 + 0.35 * glow, 0.42 + 0.40 * glow)
    else:
        c = wall
    return (c[0], c[1], c[2], 1.0)


def medical_px(u, v):
    # White building wall with a bold red cross.
    arm = (0.40 < u < 0.60 and 0.18 < v < 0.82) or (0.18 < u < 0.82 and 0.40 < v < 0.60)
    return (0.85, 0.10, 0.10, 1.0) if arm else (0.93, 0.93, 0.95, 1.0)


def parking_px(u, v):
    base = 0.22
    line = 0.85 if ((u * 8.0) % 1.0) < 0.06 else 0.0
    edge = 0.8 if (v < 0.04 or v > 0.96) else 0.0
    g = base + max(line, edge) * 0.6
    return (g, g, g * 1.02, 1.0)


def access_px(u, v):
    g = 0.27 + 0.03 * ((u * 19.0 + v * 23.0) % 1.0)
    return (g, g, g, 1.0)


def water_px(u, v):
    rip = 0.5 + 0.5 * math.sin(u * 40.0 + math.cos(v * 33.0) * 3.0)
    return (0.06 + 0.05 * rip, 0.22 + 0.10 * rip, 0.42 + 0.12 * rip, 1.0)


def write_textures():
    write_png(TRACK_DIR / "nurburgring_asphalt.png", 64, 64, asphalt_px)
    write_png(TRACK_DIR / "nurburgring_collision.png", 8, 8, lambda u, v: (0.05, 0.05, 0.06, 1.0))
    write_png(TRACK_DIR / "nurburgring_curb.png", 16, 32, curb_px)
    write_png(TRACK_DIR / "nurburgring_grass.png", 256, 256, grass_px)
    write_png(TRACK_DIR / "nurburgring_zipper.png", 128, 64, zipper_px)
    write_png(TRACK_DIR / "nbr_building.png", 96, 96, building_px)
    write_png(TRACK_DIR / "nbr_medical.png", 64, 64, medical_px)
    write_png(TRACK_DIR / "nbr_parking.png", 128, 64, parking_px)
    write_png(TRACK_DIR / "nbr_access.png", 32, 32, access_px)
    write_png(TRACK_DIR / "nbr_water.png", 128, 128, water_px)


def write_minimap(samples):
    """Top-down map rasterised into a buffer, then emitted via write_png.
    Skipped if a hand-supplied screenshot.png already exists (keeps the custom
    track image)."""
    if (TRACK_DIR / "screenshot.png").exists():
        print("[minimap] keeping existing screenshot.png")
        return
    size = 512
    margin = 24
    xs = [s["c"][0] for s in samples]
    zs = [s["c"][2] for s in samples]
    minx, maxx, minz, maxz = min(xs), max(xs), min(zs), max(zs)
    span = max(maxx - minx, maxz - minz) or 1.0
    scale = (size - 2 * margin) / span

    def to_px(x, z):
        px = margin + (x - minx) * scale
        py = margin + (z - minz) * scale
        return int(px), int(py)

    buf = [[(0.06, 0.20, 0.08)] * size for _ in range(size)]

    def plot(px, py, col):
        if 0 <= px < size and 0 <= py < size:
            buf[py][px] = col

    def line(p0, p1, col, thick=3):
        x0, y0 = p0
        x1, y1 = p1
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            for ox in range(-thick, thick + 1):
                for oy in range(-thick, thick + 1):
                    plot(x0 + ox, y0 + oy, col)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    px = [to_px(s["c"][0], s["c"][2]) for s in samples]
    for i in range(len(px)):
        line(px[i], px[(i + 1) % len(px)], (0.55, 0.55, 0.58))
    line(px[0], px[0], (1.0, 0.85, 0.1), thick=5)  # start/finish marker

    def px_fn(u, v):
        x = int(u * (size - 1))
        y = int(v * (size - 1))
        c = buf[y][x]
        return (c[0], c[1], c[2], 1.0)

    write_png(TRACK_DIR / "screenshot.png", size, size, px_fn)


# --------------------------------------------------------------------------- #
# Track XML (quads / graph / scene / materials / track).
# --------------------------------------------------------------------------- #
def write_quads_xml(samples):
    n = len(samples)
    lines = ['<?xml version="1.0"?>', "<quads>",
             '  <height-testing min="-12.000000" max="12.000000"/>',
             "  <!-- Nuerburgring GP-Strecke driveline -->"]
    for i in range(n):
        a, b = samples[i], samples[(i + 1) % n]
        p0 = edge(a, -GRAPH_HALF_WIDTH)
        p1 = edge(a, GRAPH_HALF_WIDTH)
        p2 = edge(b, GRAPH_HALF_WIDTH)
        p3 = edge(b, -GRAPH_HALF_WIDTH)
        lines.append(f'  <quad p0="{fmt_vec(p0)}" p1="{fmt_vec(p1)}" '
                     f'p2="{fmt_vec(p2)}" p3="{fmt_vec(p3)}"/>')
    lines.append("</quads>")
    (TRACK_DIR / "quads.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_graph_xml(samples):
    last = len(samples) - 1
    (TRACK_DIR / "graph.xml").write_text(
        f'<?xml version="1.0"?>\n<graph>\n'
        f'  <node-list from-quad="0" to-quad="{last}"/>\n'
        f'  <edge-loop from="0" to="{last}"/>\n</graph>\n',
        encoding="utf-8")


def check_line(sample):
    return edge(sample, -GRAPH_HALF_WIDTH * 1.25), edge(sample, GRAPH_HALF_WIDTH * 1.25)


def start_positions(samples):
    n = len(samples)
    step = max(1, int(round(START_ROW_SPACING_M / RESAMPLE_SPACING)))
    starts = []
    for row in range(START_ROWS):
        idx = (-(row + 1) * step) % n          # rows sit behind the finish line
        sm = samples[idx]
        heading = math.degrees(math.atan2(sm["t"][0], sm["t"][2]))
        for col in range(START_COLS):
            p = edge(sm, START_LATERALS[col], START_LIFT)
            starts.append((row * START_COLS + col, p, heading))
    return starts


def start_grid_self_check(starts):
    worst = float("inf")
    for i in range(len(starts)):
        for j in range(i + 1, len(starts)):
            d = vlength(vsub(starts[i][1], starts[j][1]))
            worst = min(worst, d)
    if worst < START_MIN_DISTANCE:
        raise RuntimeError(f"Start grid overlap: closest pair {worst:.2f} m apart.")
    return worst


def scenery_objects(samples, height_at):
    """Line both verges with shared library trees + undergrowth (black_forest
    style), each grounded on the terrain via height_at so nothing floats. Trees
    sit beyond the grass apron so they never touch the racing line; collidable
    pines are pushed furthest out. Deterministic via TREE_SEED. Returns a list
    of <library .../> XML lines (assets resolve from stk-assets/library/ at
    load -- nothing is copied into the track)."""
    rng = random.Random(TREE_SEED)
    weighted = [t for t, w in TREE_TYPES for _ in range(w)]
    # Far rows use cheap low-poly conifers so a dense forest stays affordable.
    far_types = ["stklib_lowPineTree_a", "stklib_lowPineTree_a",
                 "stklib_pinetree_c", "stklib_pinetree_a"]
    inner = ROAD_HALF_WIDTH + GRASS_WIDTH + TREE_MARGIN_M
    near_band = FOREST_DEPTH_M * 0.4
    lines = ["  <!-- Dense trackside pine forest: shared stk-assets/library objects -->"]
    idx = 0
    skipped = 0

    track_xz = [(s["c"][0], s["c"][2]) for s in samples]
    clear2 = TREE_TRACK_CLEARANCE * TREE_TRACK_CLEARANCE

    def on_track(x, z):
        for sx, sz in track_xz:
            if (x - sx) ** 2 + (z - sz) ** 2 < clear2:
                return True
        return False

    def grounded(x, z, sink):
        return (x, height_at(x, z) - sink, z)

    def emit(kind, p, scale):
        nonlocal idx
        lines.append(
            f'  <library name="{kind}" id="nbr_{kind}_{idx}" '
            f'{fmt_xyz_attrs(p)} hpr="0 {rng.uniform(0, 360):.1f} 0" '
            f'scale="{scale:.2f} {scale:.2f} {scale:.2f}"/>')
        idx += 1

    # NOTE: STK applies the vertical (yaw) rotation via the SECOND hpr value for
    # these library objects (matches black_forest, e.g. hpr="0 25.2 0"). Putting
    # the random angle in the first slot tilts/topples the tree -- keep it 2nd.
    for si in range(0, len(samples), TREE_LONG_STRIDE):
        sm = samples[si]
        for side in (-1.0, 1.0):
            for _ in range(TREES_PER_SIDE):
                depth = rng.uniform(0.0, FOREST_DEPTH_M)
                offset = side * (inner + depth)
                q = vadd(edge(sm, offset),
                         vmul(sm["t"], rng.uniform(-TREE_SPACING_M, TREE_SPACING_M)))
                if on_track(q[0], q[2]):     # would land on another track segment
                    skipped += 1
                    continue
                p = grounded(q[0], q[2], 0.2)
                if depth > near_band:                       # back of the forest
                    emit(rng.choice(far_types), p, rng.uniform(0.85, 1.45))
                else:                                       # front rows: full mix
                    emit(rng.choice(weighted), p, rng.uniform(0.85, 1.55))
            if rng.random() < 0.5:   # undergrowth tight against the verge
                uoff = side * (ROAD_HALF_WIDTH + GRASS_WIDTH + rng.uniform(0.5, 3.5))
                uq = vadd(edge(sm, uoff), vmul(sm["t"], rng.uniform(-6.0, 6.0)))
                if on_track(uq[0], uq[2]):
                    skipped += 1
                    continue
                up = grounded(uq[0], uq[2], 0.1)
                emit(rng.choice(UNDERGROWTH_TYPES), up, rng.uniform(0.7, 1.4))
    print(f"[scenery] placed {idx} library objects (dense forest), "
          f"skipped {skipped} on-track")
    return lines


def item_lines(samples):
    """Scatter pickups along the racing line: rows of bonus boxes, nitro, and
    bananas (compactifications). Placed on the road surface with the local
    surface normal so they sit flush. Skips the start/finish straight."""
    n = len(samples)

    def attrs(si, lateral):
        sm = samples[si % n]
        p = edge(sm, lateral, 0.2)
        return (f'{fmt_xyz_attrs(p)} surface-normal="{fmt_vec(sm["n"])}" '
                f'drop="false"')

    lines = ["  <!-- Pickups: bonus boxes, nitro, bananas (compactifications) -->"]
    # Rows of three bonus boxes across the track.
    for si in range(18, n - 12, 16):
        for lat in (-3.2, 0.0, 3.2):
            lines.append(f'  <item {attrs(si, lat)}/>')
    # Nitro: small pairs frequently, a big one occasionally.
    for si in range(10, n - 12, 13):
        lines.append(f'  <small-nitro {attrs(si, -2.6)}/>')
        lines.append(f'  <small-nitro {attrs(si, 2.6)}/>')
    for si in range(24, n - 12, 55):
        lines.append(f'  <big-nitro {attrs(si, 0.0)}/>')
    # Bananas weaving across the road.
    for k, si in enumerate(range(8, n - 12, 9)):
        lines.append(f'  <banana {attrs(si, (k % 5 - 2) * 1.7)}/>')
    return lines


def write_scene_xml(samples, height_at, landmark_lines):
    n = len(samples)
    finish = samples[0]
    w1 = samples[n // 4]
    w2 = samples[n // 2]
    w3 = samples[(3 * n) // 4]
    fa, fb = check_line(finish)
    a1, b1 = check_line(w1)
    a2, b2 = check_line(w2)
    a3, b3 = check_line(w3)

    lines = [
        '<?xml version="1.0"?>',
        "<scene>",
        # Black Forest's sun verbatim (position + warm diffuse/specular + cool
        # ambient + fog colour); only fog-start/end widened for this big circuit
        # so the visible sun + lighting match Black Forest.
        '  <sun fog="true" fog-color="65 130 181" fog-max="0.7" fog-start="150.00" fog-end="750.00" xyz="4.08 375.58 -312.71" sun-specular="255 204 50" sun-diffuse="255 205 50" ambient="75 90 145"/>',
        '  <sky-color rgb="13 165 255"/>',
        '  <sky-box texture="blue_sky_top.jpg blue_sky_top.jpg blue_sky_east.jpg blue_sky_west.jpg blue_sky_south.jpg blue_sky_north.jpg" sh-texture="blue_sky_top.jpg blue_sky_top.jpg blue_sky_east.jpg blue_sky_west.jpg blue_sky_south.jpg blue_sky_north.jpg"/>',
        '  <camera far="1600"/>',
        '  <track model="nurburgring_road_visual.spm" x="0" y="0" z="0">',
        '    <static-object model="nurburgring_terrain.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_road_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="nurburgring_grass_col_left.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="nurburgring_grass_col_right.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="nurburgring_grass_left.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_grass_right.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_curb_left.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_curb_right.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_zippers.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost-texture" shadow-pass="false"/>',
        *landmark_lines,
        "  </track>",
        "  <checks>",
        '    <check-lap kind="lap" active="true" same-group="0" other-ids="1"/>',
        f'    <check-line kind="activate" same-group="1" other-ids="2" p1="{fmt_vec(a1)}" p2="{fmt_vec(b1)}"/>',
        f'    <check-line kind="activate" same-group="2" other-ids="3" p1="{fmt_vec(a2)}" p2="{fmt_vec(b2)}"/>',
        f'    <check-line kind="activate" same-group="3" other-ids="4" p1="{fmt_vec(a3)}" p2="{fmt_vec(b3)}"/>',
        f'    <check-line kind="lap" active="false" same-group="4" other-ids="1" p1="{fmt_vec(fa)}" p2="{fmt_vec(fb)}"/>',
        "  </checks>",
    ]
    for idx, p, heading in start_positions(samples):
        lines.append(f'  <start position="{idx}" {fmt_xyz_attrs(p)} h="{heading:.2f}"/>')
    lines.extend(item_lines(samples))
    lines.extend(scenery_objects(samples, height_at))
    lines.append("</scene>")
    (TRACK_DIR / "scene.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_materials_xml():
    (TRACK_DIR / "materials.xml").write_text(
        '<?xml version="1.0"?>\n<materials>\n'
        # Visual road is render-only (ignore=Y). All collision comes from the
        # separate physics-only ground_collision mesh -- this mirrors the mobius
        # track and avoids the Vulkan race where the GPU-uploaded visual mesh's
        # CPU vertex data is freed before convertTrackToBullet reads it.
        '  <material name="nurburgring_asphalt.png" ignore="Y" backface-culling="N"/>\n'
        '  <material name="nurburgring_collision.png" high-adhesion="Y" has-gravity="Y" backface-culling="N"/>\n'
        '  <material name="nurburgring_curb.png" high-adhesion="Y" backface-culling="N"/>\n'
        # Grass: drivable but slows karts. Applies to the apron + grass collision\n
        # meshes; the visual (ghost) grass/terrain copies ignore physics props.\n
        '  <material name="nurburgring_grass.png" has-gravity="Y" slowdown-time="1.5" max-speed="0.5" backface-culling="N"/>\n'
        '  <material name="nurburgring_zipper.png" shader="alphablend" ignore="N" backface-culling="N">\n'
        '    <zipper duration="2.5" max-speed-increase="12.0" fade-out-time="2.0" speed-gain="5.0" min-speed="0.0"/>\n'
        '  </material>\n'
        '  <material name="nbr_building.png" ignore="Y" backface-culling="N"/>\n'
        '  <material name="nbr_medical.png" ignore="Y" backface-culling="N"/>\n'
        '  <material name="nbr_parking.png" ignore="Y" backface-culling="N"/>\n'
        '  <material name="nbr_access.png" ignore="Y" backface-culling="N"/>\n'
        '  <material name="nbr_water.png" ignore="Y" backface-culling="N"/>\n'
        "</materials>\n",
        encoding="utf-8")


def write_track_xml():
    (TRACK_DIR / "track.xml").write_text(
        '<?xml version="1.0"?>\n'
        '<track  name           = "Nürburgring GP"\n'
        '        version        = "7"\n'
        '        groups         = "minkowski"\n'
        '        designer       = "Robson Christie (geometry: OpenStreetMap)"\n'
        '        screenshot     = "screenshot.png"\n'
        '        music          = "rennfieber.music"\n'
        '        smooth-normals = "true"\n'
        '        default-number-of-laps = "3"\n'
        '        reverse        = "N"\n'
        '        clouds         = "N"\n'
        '        is-during-day  = "Y"\n'
        '        shadows        = "Y">\n'
        "</track>\n",
        encoding="utf-8")


def write_license():
    (TRACK_DIR / "LICENSE.txt").write_text(
        "Nuerburgring GP-Strecke track for MinkowskiKart.\n"
        "Generated by BlenderConversionScripts/generate_nurburgring_track.py.\n\n"
        "Circuit geometry derived from OpenStreetMap data.\n"
        "  (c) OpenStreetMap contributors, licensed under the ODbL.\n"
        "  https://www.openstreetmap.org/copyright\n\n"
        "Elevation sampled from OpenTopoData (SRTM 30m).\n"
        "  https://www.opentopodata.org/  /  SRTM: NASA/USGS, public domain.\n\n"
        "Textures and meshes are procedural project-local generated assets.\n"
        'Note: "Nuerburgring" is a trademark; this fan track is for non-commercial use.\n',
        encoding="utf-8")


def write_manifest(samples, total_len, used_elevation):
    (TRACK_DIR / "manifest.json").write_text(json.dumps({
        "track": "nurburgring_gp",
        "layout": "GP-Strecke",
        "centerline_nodes": len(samples),
        "lap_length_m": round(total_len, 1),
        "elevation_source": "OpenTopoData SRTM30m" if used_elevation else "flat (DEM unavailable)",
        "geometry_source": "OpenStreetMap (ODbL)",
        "meshes": MESH_FILES,
    }, indent=2), encoding="utf-8")


# --------------------------------------------------------------------------- #
# Main.
# --------------------------------------------------------------------------- #
def main():
    TRACK_DIR.mkdir(parents=True, exist_ok=True)

    osm = fetch_osm()
    loop_latlon = extract_loop(osm)

    lat0 = sum(p[0] for p in loop_latlon) / len(loop_latlon)
    lon0 = sum(p[1] for p in loop_latlon) / len(loop_latlon)
    loop_xz = [project(la, lo, lat0, lon0) for la, lo in loop_latlon]
    loop_xz = resample_loop(loop_xz, RESAMPLE_SPACING)

    # Recover lat/lon at the resampled points for elevation lookup.
    resampled_latlon = [
        (lat0 + math.degrees(z / EARTH_R),
         lon0 + math.degrees(x / (EARTH_R * math.cos(math.radians(lat0)))))
        for (x, z) in loop_xz
    ]
    elevations = fetch_elevations(resampled_latlon)
    used_elevation = elevations is not None
    base = min(elevations) if elevations else 0.0

    samples = build_samples(loop_xz, elevations)
    total_len = samples[-1]["s"] + vlength(vsub(samples[0]["c"], samples[-1]["c"]))
    print(f"[geo] {len(samples)} samples, lap length {total_len:.0f} m, "
          f"elevation={'real' if used_elevation else 'flat'}")

    # Meshes.
    meshes = build_meshes(samples)
    for key_, mesh in meshes.items():
        if len(mesh["verts"]) > 65535:
            raise RuntimeError(f"{key_} exceeds SPM vertex limit: {len(mesh['verts'])}")
        write_spm(TRACK_DIR / MESH_FILES[key_], mesh)
    print(f"[mesh] wrote {len(meshes)} SPM meshes "
          f"(max verts {max(len(m['verts']) for m in meshes.values())})")

    # Rolling terrain (grounds the trackside trees; visual-only).
    terrain_mesh, height_at = build_terrain(samples, base, lat0, lon0)
    if len(terrain_mesh["verts"]) > 65535:
        raise RuntimeError(f"terrain exceeds SPM vertex limit: {len(terrain_mesh['verts'])}")
    write_spm(TRACK_DIR / TERRAIN_FILE, terrain_mesh)

    # Map landmarks from the GP overview (buildings/medical/water/parking).
    landmark_meshes, landmark_lines = build_landmarks(samples, height_at)
    for fname, mesh in landmark_meshes.items():
        write_spm(TRACK_DIR / fname, mesh)

    # Textures.
    write_textures()
    write_minimap(samples)

    # XML.
    write_quads_xml(samples)
    write_graph_xml(samples)
    write_scene_xml(samples, height_at, landmark_lines)
    write_materials_xml()
    write_track_xml()
    write_license()
    write_manifest(samples, total_len, used_elevation)

    # Self-checks.
    worst = start_grid_self_check(start_positions(samples))
    assert len(samples) >= 16, "too few centerline samples"
    print(f"[check] start grid min spacing {worst:.2f} m; "
          f"quads={len(samples)} == graph nodes={len(samples)} OK")
    print(f"[done] -> {TRACK_DIR}")


if __name__ == "__main__":
    main()
