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
                 u0=0.0, u1=1.0):
    verts, normals, uvs, indices = [], [], [], []
    n = len(samples)
    for sm in samples:
        verts.append(edge(sm, off_left, dy))
        verts.append(edge(sm, off_right, dy))
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
    # Collision: road + grass apron in one physics surface so off-track is solid.
    meshes["ground_collision"] = build_ribbon(
        "Nurburgring_Ground_Collision", "nurburgring_collision.png",
        samples, -(ROAD_HALF_WIDTH + GRASS_WIDTH), (ROAD_HALF_WIDTH + GRASS_WIDTH),
        dy=-0.02)
    # Grass apron, two strips (visual), slightly below the road lip.
    meshes["grass_left"] = build_ribbon(
        "Nurburgring_Grass_Left", "nurburgring_grass.png",
        samples, -(ROAD_HALF_WIDTH + GRASS_WIDTH), -ROAD_HALF_WIDTH, dy=-0.05,
        u0=0.0, u1=GRASS_WIDTH / UV_TILE_LENGTH)
    meshes["grass_right"] = build_ribbon(
        "Nurburgring_Grass_Right", "nurburgring_grass.png",
        samples, ROAD_HALF_WIDTH, (ROAD_HALF_WIDTH + GRASS_WIDTH), dy=-0.05,
        u0=0.0, u1=GRASS_WIDTH / UV_TILE_LENGTH)
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
    "ground_collision": "nurburgring_ground_collision.spm",
    "grass_left": "nurburgring_grass_left.spm",
    "grass_right": "nurburgring_grass_right.spm",
    "curb_left": "nurburgring_curb_left.spm",
    "curb_right": "nurburgring_curb_right.spm",
}


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


def grass_px(u, v):
    n = (math.sin(u * 190.0 + v * 70.0) * 0.5 + 0.5)
    return (0.10 + 0.05 * n, 0.34 + 0.10 * n, 0.10 + 0.04 * n, 1.0)


def write_textures():
    write_png(TRACK_DIR / "nurburgring_asphalt.png", 64, 64, asphalt_px)
    write_png(TRACK_DIR / "nurburgring_collision.png", 8, 8, lambda u, v: (0.05, 0.05, 0.06, 1.0))
    write_png(TRACK_DIR / "nurburgring_curb.png", 16, 32, curb_px)
    write_png(TRACK_DIR / "nurburgring_grass.png", 64, 64, grass_px)


def write_minimap(samples):
    """Top-down map rasterised into a buffer, then emitted via write_png."""
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


def write_scene_xml(samples):
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
        '  <sky-color rgb="120 150 190"/>',
        '  <camera far="1600"/>',
        '  <sun xyz="200 400 -150" sun-diffuse="255 252 240" sun-specular="255 255 250" ambient="120 122 130" fog="false"/>',
        '  <track model="nurburgring_road_visual.spm" x="0" y="0" z="0">',
        '    <static-object model="nurburgring_ground_collision.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="physics-only"/>',
        '    <static-object model="nurburgring_grass_left.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_grass_right.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_curb_left.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
        '    <static-object model="nurburgring_curb_right.spm" xyz="0 0 0" hpr="0 0 0" scale="1 1 1" interaction="ghost"/>',
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
        '  <material name="nurburgring_grass.png" backface-culling="N"/>\n'
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
        '        music          = "highway_gravel.music"\n'
        '        smooth-normals = "true"\n'
        '        default-number-of-laps = "3"\n'
        '        reverse        = "N"\n'
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

    # Textures.
    write_textures()
    write_minimap(samples)

    # XML.
    write_quads_xml(samples)
    write_graph_xml(samples)
    write_scene_xml(samples)
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
