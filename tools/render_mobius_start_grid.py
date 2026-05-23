import json
import math
import os
import xml.etree.ElementTree as ET

import bpy
from mathutils import Vector

ROOT = r"C:\Users\robso\OneDrive\Desktop\PersonalGames\TestGame"
TRACK_DIR = os.path.join(ROOT, "stk-assets", "tracks", "mobius_track")
SCENE_XML = os.path.join(TRACK_DIR, "scene.xml")
OUT_DIR = os.path.join(ROOT, "reports", "mobius_start_grid")
os.makedirs(OUT_DIR, exist_ok=True)

for obj in list(bpy.data.objects):
    if obj.name.startswith(("StartGrid_", "GridCamera_", "GridLight_")):
        bpy.data.objects.remove(obj, do_unlink=True)

tree = ET.parse(SCENE_XML)
root = tree.getroot()
starts = []
for node in root.findall("start"):
    starts.append(
        {
            "position": int(node.attrib["position"]),
            "x": float(node.attrib["x"]),
            "y": float(node.attrib["y"]),
            "z": float(node.attrib["z"]),
            "h": float(node.attrib["h"]),
        }
    )
starts.sort(key=lambda item: item["position"])

def vec(start):
    return Vector((start["x"], start["y"], start["z"]))

def mat(name, color):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = color
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Emission Color"].default_value = color
        bsdf.inputs["Emission Strength"].default_value = 1.6
    return material

lane_mats = [
    mat("StartGrid_LeftLane_Cyan", (0.1, 0.9, 1.0, 1.0)),
    mat("StartGrid_CenterLane_Yellow", (1.0, 0.85, 0.05, 1.0)),
    mat("StartGrid_RightLane_Magenta", (1.0, 0.15, 0.9, 1.0)),
]
line_mat = mat("StartGrid_FinishLine_Orange", (1.0, 0.42, 0.0, 1.0))
text_mat = mat("StartGrid_Text_White", (1.0, 1.0, 1.0, 1.0))

for obj in bpy.data.objects:
    if any(key in obj.name for key in ("Collision", "Safety", "reset_fall", "Reset_Fall", "Star_Sphere", "Sun_", "Planet_", "Black_Hole")):
        obj.hide_render = True
        obj.hide_viewport = True
    elif obj.name == "Mobius_Road_Visual":
        for slot in obj.material_slots:
            if slot.material:
                slot.material.diffuse_color = (0.16, 0.18, 0.20, 1.0)

for start in starts:
    p = vec(start) + Vector((0, 1.6, 0))
    col = start["position"] % 3
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1.35, location=p)
    marker = bpy.context.object
    marker.name = f"StartGrid_marker_{start['position']:02d}"
    marker.data.materials.append(lane_mats[col])

    heading_rad = math.radians(start["h"])
    bpy.ops.mesh.primitive_cone_add(vertices=3, radius1=1.25, depth=2.2, location=p + Vector((0, 1.2, 0)))
    arrow = bpy.context.object
    arrow.name = f"StartGrid_heading_{start['position']:02d}"
    arrow.rotation_euler[1] = math.radians(90)
    arrow.rotation_euler[2] = -heading_rad
    arrow.data.materials.append(lane_mats[col])

    bpy.ops.object.text_add(location=p + Vector((0, 3.2, 0)))
    label = bpy.context.object
    label.name = f"StartGrid_label_{start['position']:02d}"
    label.data.body = str(start["position"])
    label.data.align_x = "CENTER"
    label.data.align_y = "CENTER"
    label.data.size = 2.2
    label.data.materials.append(text_mat)

def make_curve(name, points, material, bevel=0.18):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 1
    curve.bevel_depth = bevel
    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for point, co in zip(spline.points, points):
        point.co = (co[0], co[1], co[2], 1)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(material)
    return obj

finish_lines = [n for n in root.findall("./checks/check-line") if n.attrib.get("kind") == "lap"]
if finish_lines:
    lap = finish_lines[-1]
    p1 = Vector(tuple(float(x) for x in lap.attrib["p1"].split()))
    p2 = Vector(tuple(float(x) for x in lap.attrib["p2"].split()))
    make_curve("StartGrid_lap_line", [p1 + Vector((0, 2.1, 0)), p2 + Vector((0, 2.1, 0))], line_mat, 0.28)

center = sum((vec(s) for s in starts), Vector()) / len(starts)
rows = [[s for s in starts if s["position"] // 3 == row] for row in range(4)]
row_centers = [sum((vec(s) for s in row), Vector()) / len(row) for row in rows]
for row in rows:
    make_curve(
        f"StartGrid_row_{row[0]['position'] // 3}",
        [vec(row[0]) + Vector((0, 2.2, 0)), vec(row[-1]) + Vector((0, 2.2, 0))],
        line_mat,
        0.12,
    )

def look_at(obj, target, up_axis="Y"):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", up_axis).to_euler()

def render_camera(name, location, lens, filename):
    camera_data = bpy.data.cameras.new(name)
    camera = bpy.data.objects.new(name, camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = Vector(location)
    camera_data.lens = lens
    camera_data.clip_end = 1000
    look_at(camera, center, "Y")
    bpy.context.scene.camera = camera
    bpy.context.scene.render.filepath = os.path.join(OUT_DIR, filename)
    bpy.ops.render.render(write_still=True)
    return bpy.context.scene.render.filepath

bpy.ops.object.light_add(type="AREA", location=center + Vector((0, 52, 0)))
light = bpy.context.object
light.name = "GridLight_start_grid_area"
light.data.energy = 650
light.data.size = 45

bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
if hasattr(bpy.context.scene, "display"):
    bpy.context.scene.display.shading.light = "STUDIO"
    bpy.context.scene.display.shading.color_type = "MATERIAL"
    bpy.context.scene.display.shading.background_type = "VIEWPORT"
    bpy.context.scene.display.shading.background_color = (0.02, 0.025, 0.035)
bpy.context.scene.render.resolution_x = 1600
bpy.context.scene.render.resolution_y = 1000
bpy.context.scene.world.color = (0.0, 0.0, 0.0)

paths = {
    "overview": render_camera("GridCamera_overview", center + Vector((34, 46, 48)), 34, "mobius_start_grid_overview.png"),
    "top": render_camera("GridCamera_top", center + Vector((0, 92, 0)), 46, "mobius_start_grid_top.png"),
    "low": render_camera("GridCamera_low", center + Vector((-16, 13, -34)), 30, "mobius_start_grid_low_angle.png"),
}

pairwise = []
for i, a in enumerate(starts):
    for b in starts[i + 1 :]:
        pairwise.append((round((vec(a) - vec(b)).length, 3), a["position"], b["position"]))
pairwise.sort()

row_gaps = [
    round((row_centers[i] - row_centers[i + 1]).length, 3)
    for i in range(len(row_centers) - 1)
]
lane_gaps = []
for row in rows:
    lane_gaps.append(round((vec(row[0]) - vec(row[1])).length, 3))
    lane_gaps.append(round((vec(row[1]) - vec(row[2])).length, 3))

report = {
    "start_count": len(starts),
    "positions": starts,
    "closest_pair": {"distance": pairwise[0][0], "positions": [pairwise[0][1], pairwise[0][2]]},
    "row_center_gaps": row_gaps,
    "lane_gaps_min": min(lane_gaps),
    "lane_gaps_max": max(lane_gaps),
    "heading_range": [min(s["h"] for s in starts), max(s["h"] for s in starts)],
    "screenshots": paths,
}
with open(os.path.join(OUT_DIR, "mobius_start_grid_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=2)
print(json.dumps(report, indent=2))
