"""
Blender headless decimation script for SPM asset pipeline.
Imports an OBJ, applies Blender's Decimate modifier (edge-collapse),
and exports two OBJ files:
  <output_base>.obj       -- full-quality, decimated to fit SPM 65535-vert limit
  <output_base>_low.obj   -- low-poly LOD, coarse-grained (not stride-sampled)

Usage (headless):
  blender --background --python decimate_for_spm.py -- \
      <input.obj> <output_base> [full_ratio] [low_ratio]

Arguments after '--':
  input.obj      Source OBJ file path.
  output_base    Base path/name for output OBJs (no extension).
  full_ratio     Decimate ratio for full mesh (default 0.45).
                 Blender decimate ratio is face_count_out / face_count_in.
                 0.45 typically reduces 130k verts to ~55-60k.
  low_ratio      Decimate ratio for low-poly LOD (default 0.05).
                 Produces a coarse but solid-looking mesh.
"""

import bpy
import sys
import os

# --- parse args after '--' ---
argv = sys.argv
if '--' in argv:
    args = argv[argv.index('--') + 1:]
else:
    print("ERROR: no arguments after '--'")
    sys.exit(1)

if len(args) < 2:
    print(__doc__)
    sys.exit(1)

input_obj   = args[0]
output_base = args[1]
full_ratio  = float(args[2]) if len(args) > 2 else 0.45
low_ratio   = float(args[3]) if len(args) > 3 else 0.05

print(f"Input:       {input_obj}")
print(f"Output base: {output_base}")
print(f"Full ratio:  {full_ratio}")
print(f"Low ratio:   {low_ratio}")

# --- clear default scene ---
bpy.ops.wm.read_factory_settings(use_empty=True)
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

def import_obj(path):
    """Import OBJ and return the imported mesh object."""
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=path)
    new_objs = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    if not new_objs:
        raise RuntimeError(f"No mesh imported from {path}")
    # Join all imported objects into one
    bpy.ops.object.select_all(action='DESELECT')
    for o in new_objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = new_objs[0]
    if len(new_objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    print(f"  Imported: {len(obj.data.vertices)} verts, {len(obj.data.polygons)} faces")
    return obj

def apply_decimate(obj, ratio):
    """Add and apply a Decimate modifier at the given ratio."""
    mod = obj.modifiers.new(name='Decimate', type='DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = ratio
    mod.use_collapse_triangulate = True   # keep output as triangles
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    print(f"  After decimate({ratio}): {len(obj.data.vertices)} verts, {len(obj.data.polygons)} faces")
    return obj

def export_obj(obj, path):
    """Export a single mesh object to OBJ."""
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.obj_export(
        filepath=path,
        export_selected_objects=True,
        export_uv=True,
        export_normals=False,     # omit per-corner normals; obj_to_spm.py computes
                                  # smooth normals from geometry, which dedups verts
                                  # by (pos, uv) only -- avoids 5x vertex inflation
        export_materials=False,   # keep OBJ self-contained, texture ref via SPM
        export_triangulated_mesh=True,
        global_scale=1.0,
    )
    print(f"  Exported: {path}  ({os.path.getsize(path)//1024} KB)")

# ---- FULL MESH ----
print("\n=== Full-quality mesh ===")
obj_full = import_obj(input_obj)

# Auto-reduce if still over vertex limit after initial decimate
target_ratio = full_ratio
while True:
    # Re-import fresh each time to avoid double-decimating
    bpy.ops.object.select_all(action='DESELECT')
    obj_full.select_set(True)
    bpy.data.objects.remove(obj_full, do_unlink=True)
    obj_full = import_obj(input_obj)
    obj_full = apply_decimate(obj_full, target_ratio)
    vcount = len(obj_full.data.vertices)
    if vcount <= 65535:
        break
    target_ratio *= 0.8
    print(f"  Still {vcount} verts > 65535, retrying with ratio={target_ratio:.3f}")

out_full = output_base + '.obj'
export_obj(obj_full, out_full)

# ---- LOW-POLY LOD ----
print("\n=== Low-poly LOD ===")
# Re-import fresh for the low-poly pass
bpy.ops.object.select_all(action='DESELECT')
obj_full.select_set(True)
bpy.data.objects.remove(obj_full, do_unlink=True)

obj_low = import_obj(input_obj)
obj_low = apply_decimate(obj_low, low_ratio)
out_low = output_base + '_low.obj'
export_obj(obj_low, out_low)

print("\nDone.")
