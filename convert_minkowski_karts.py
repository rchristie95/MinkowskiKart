import os
import sys
import shutil
import math
from pathlib import Path
from zipfile import ZipFile
import xml.etree.ElementTree as ET

# Add script directory to sys.path so we can import the generator's SPM tools
PROJECT_ROOT = Path(__file__).resolve().parent
sys.path.append(str(PROJECT_ROOT / "BlenderConversionScripts"))

try:
    import bpy
    import mathutils
    from generate_mobius_track import write_spm, blender_object_to_mesh_dict, normalize_mesh_in_place
except ImportError as e:
    print(f"Error importing modules: {e}")
    sys.exit(1)


DEFAULT_KART_EXPORT_SCALE = 1.5

KART_SOURCE_DIR = Path(os.environ.get("MINKOWSKI_KART_SOURCE_DIR", r"C:\Users\robso\Downloads\Karts"))

KART_SOURCE_NAMES = {
    "curie": "Curie",
    "einstein": "Einstein",
    "feynman": "Feynman",
    "maxwell": "Maxwell",
    "minkowski": "Minkowski",
    "newton": "Newton",
    "noether": "Noether",
    "planck": "Planck",
    "oppenheimer": "Oppenheimer",
}

# Source meshes are authored with inconsistent yaw. Blender +Y is the intended
# forward axis before the STK exporter converts coordinates to game space.
KART_YAW_DEGREES = {
    "curie": -90.0,
    "einstein": -90.0,
    "feynman": -90.0,
    "maxwell": -90.0,
    "minkowski": 180.0,
    "newton": -90.0,
    "noether": 180.0,
    "planck": 135.0,
    "oppenheimer": -90.0,
}

KART_STATS_PROFILE = {
    "curie": "adiumy",
    "einstein": "amanda",
    "feynman": "beastie",
    "maxwell": "emule",
    "minkowski": "gavroche",
    "newton": "gnu",
    "noether": "hexley",
    "planck": "kiki",
    "oppenheimer": "nolok",
}


def reset_kart_dir_from_source(kart_name, kart_dir):
    source_name = KART_SOURCE_NAMES[kart_name]
    zip_path = KART_SOURCE_DIR / f"{source_name}_Racer.zip"
    if not zip_path.exists():
        raise FileNotFoundError(f"Missing source racer archive: {zip_path}")

    if kart_dir.exists():
        for child in kart_dir.iterdir():
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        kart_dir.mkdir(parents=True)

    with ZipFile(zip_path) as archive:
        archive.extractall(kart_dir)


def copy_source_icon(kart_name, kart_dir):
    source_name = KART_SOURCE_NAMES[kart_name]
    candidates = sorted(KART_SOURCE_DIR.glob(f"{source_name}*_Graphic.png"))
    if not candidates:
        print(f"No GUI graphic found for {kart_name} in {KART_SOURCE_DIR}")
        return
    shutil.copy2(candidates[0], kart_dir / f"{kart_name}_icon.png")


def load_official_kart_profiles():
    profiles = {}
    root = ET.parse(PROJECT_ROOT / "data" / "official_karts.xml").getroot()
    for kart in root.findall("kart"):
        name = kart.get("name")
        if not name:
            continue
        profiles[name] = {
            "type": kart.get("type", "medium"),
            "width": kart.get("width", "1.0"),
            "height": kart.get("height", "1.0"),
            "length": kart.get("length", "1.0"),
            "gravity_shift": kart.get("gravity-shift", "0 0.35 0"),
        }
    return profiles


def get_stats_profile(kart_name, official_profiles):
    source_profile = KART_STATS_PROFILE[kart_name]
    if source_profile not in official_profiles:
        raise KeyError(f"Missing official kart profile '{source_profile}'")
    return source_profile, official_profiles[source_profile]


def get_local_bounds(mesh):
    coords = [vertex.co for vertex in mesh.vertices]
    mins = [min(co[i] for co in coords) for i in range(3)]
    maxs = [max(co[i] for co in coords) for i in range(3)]
    return mins, maxs


def get_world_bounds(obj):
    positions = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    mins = [min(position[i] for position in positions) for i in range(3)]
    maxs = [max(position[i] for position in positions) for i in range(3)]
    return mins, maxs


def recenter_and_ground_mesh(obj, horizontal=True):
    mesh = obj.data
    mins, maxs = get_local_bounds(mesh)
    center_x = (mins[0] + maxs[0]) * 0.5 if horizontal else 0.0
    center_y = (mins[1] + maxs[1]) * 0.5 if horizontal else 0.0
    min_z = mins[2]
    for vertex in mesh.vertices:
        vertex.co.x -= center_x
        vertex.co.y -= center_y
        vertex.co.z -= min_z
    mesh.update()


def convert_kart(kart_name, karts_dir):
    official_profiles = load_official_kart_profiles()
    kart_dir = karts_dir / kart_name
    print(f"\nProcessing kart: {kart_name}")
    reset_kart_dir_from_source(kart_name, kart_dir)
    
    # Find OBJ or GLB
    objs = list(kart_dir.rglob("*.obj"))
    glbs = list(kart_dir.rglob("*.glb"))
    
    if not objs and not glbs:
        print(f"No OBJ or GLB found for {kart_name}, skipping.")
        return
        
    bpy.ops.wm.read_factory_settings(use_empty=True)
    before = set(bpy.data.objects.keys())
    
    if objs:
        bpy.ops.wm.obj_import(filepath=str(objs[0]))
    else:
        bpy.ops.import_scene.gltf(filepath=str(glbs[0]))
        
    added_names = set(bpy.data.objects.keys()) - before
    mesh_objects = [bpy.data.objects[name] for name in added_names if bpy.data.objects[name].type == 'MESH']
    
    if not mesh_objects:
        print(f"Failed to import mesh objects for {kart_name}.")
        return
        
    bpy.ops.object.select_all(action='DESELECT')
    for o in mesh_objects:
        o.select_set(True)
        if o.data.uv_layers:
            o.data.uv_layers[0].name = "UVMap"
            
    bpy.context.view_layer.objects.active = mesh_objects[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    
    if len(mesh_objects) > 1:
        try:
            bpy.ops.object.join()
        except:
            pass
        
    obj = bpy.context.view_layer.objects.active
    
    mesh = obj.data
    yaw_degrees = KART_YAW_DEGREES.get(kart_name, 0.0)
    if abs(yaw_degrees) > 0.001:
        yaw = mathutils.Matrix.Rotation(math.radians(yaw_degrees), 4, "Z")
        for vertex in mesh.vertices:
            vertex.co = yaw @ vertex.co
        mesh.update()

    mins, maxs = get_world_bounds(obj)
    center = tuple((mins[i] + maxs[i]) * 0.5 for i in range(3))
    
    try:
        triangulate = obj.modifiers.new("Triangulate", "TRIANGULATE")
        bpy.ops.object.modifier_apply(modifier=triangulate.name)
    except:
        pass
    
    for polygon in obj.data.polygons:
        polygon.use_smooth = True

    extent = max(maxs[i] - mins[i] for i in range(3))
    scale = DEFAULT_KART_EXPORT_SCALE / extent if extent > 0.001 else 1.0
    
    for vertex in mesh.vertices:
        vertex.co.x = (vertex.co.x - center[0]) * scale
        vertex.co.y = (vertex.co.y - center[1]) * scale
        vertex.co.z = (vertex.co.z - center[2]) * scale
    
    world_positions = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    min_z = min(position[2] for position in world_positions)
    for vertex in mesh.vertices:
        vertex.co.z -= min_z
        
    obj.location = (0.0, 0.0, 0.0)
    mesh.update()
    
    recenter_and_ground_mesh(obj)

    source_images = [
        p
        for p in kart_dir.rglob("*.png")
        if p.parent != kart_dir and not p.name.endswith("_icon.png")
    ]
    source_images += [p for p in kart_dir.rglob("*.jpg") if p.parent != kart_dir]
    tex_name = f"{kart_name}.png"
    if source_images:
        dest = kart_dir / tex_name
        shutil.copy2(source_images[0], dest)
    else:
        # GLB files can carry embedded textures; pick the largest loaded image
        # instead of relying on the active joined object's first material.
        embedded_images = [
            image for image in bpy.data.images
            if image.size[0] > 0 and image.size[1] > 0
        ]
        if embedded_images:
            img = max(embedded_images, key=lambda image: image.size[0] * image.size[1])
            copy = img.copy()
            try:
                copy.filepath_raw = str(kart_dir / tex_name)
                copy.file_format = 'PNG'
                copy.save()
            finally:
                bpy.data.images.remove(copy)
    
    mesh_dict = blender_object_to_mesh_dict(kart_name, obj, tex_name)
    spm_path = kart_dir / f"{kart_name}.spm"
    write_spm(spm_path, mesh_dict)
    
    icon_name = f"{kart_name}_icon.png"
    copy_source_icon(kart_name, kart_dir)
    
    profile_name, stats = get_stats_profile(kart_name, official_profiles)
    mins, maxs = get_local_bounds(mesh)
    ground_contact = mins[2]
    size = tuple(maxs[i] - mins[i] for i in range(3))
    print(
        "  Self-check: "
        f"yaw={yaw_degrees:.1f} scale={scale:.6f} "
        f"bbox={size[0]:.3f}x{size[1]:.3f}x{size[2]:.3f} "
        f"ground-z={ground_contact:.6f} "
        f"stats={profile_name}/{stats['type']}"
    )

    kart_xml = f"""<?xml version="1.0"?>
<kart name="{kart_name.title()}"
      version="3"
      model-file="{kart_name}.spm"
      icon-file="{icon_name}"
      minimap-icon-file="{icon_name}"
      type="{stats['type']}"
      width="{stats['width']}"
      height="{stats['height']}"
      length="{stats['length']}"
      groups="minkowski">
  <center gravity-shift="{stats['gravity_shift']}" />
</kart>"""
    (kart_dir / "kart.xml").write_text(kart_xml, encoding="utf-8")
    
    # Remove any existing materials.xml
    if (kart_dir / "materials.xml").exists():
        (kart_dir / "materials.xml").unlink()
    
    print(f"Successfully processed kart: {kart_name}")

if __name__ == "__main__":
    karts_dir = PROJECT_ROOT / "stk-assets" / "karts"
    default_karts = [
        "curie",
        "einstein",
        "feynman",
        "maxwell",
        "minkowski",
        "newton",
        "noether",
        "planck",
        "oppenheimer",
    ]
    script_args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    requested_karts = [arg.lower() for arg in script_args]
    karts_to_process = requested_karts or default_karts
    unknown_karts = sorted(set(karts_to_process) - set(default_karts))
    if unknown_karts:
        raise SystemExit(f"Unknown Minkowski kart(s): {', '.join(unknown_karts)}")
    for kart in karts_to_process:
        convert_kart(kart, karts_dir)
