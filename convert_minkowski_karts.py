import os
import sys
import shutil
from pathlib import Path

# Add script directory to sys.path so we can import the generator's SPM tools
sys.path.append(r"C:\Users\robso\OneDrive\Desktop\PersonalGames\TestGame\BlenderConversionScripts")

try:
    import bpy
    from generate_mobius_track import write_spm, blender_object_to_mesh_dict, normalize_mesh_in_place
except ImportError as e:
    print(f"Error importing modules: {e}")
    sys.exit(1)

def convert_kart(kart_name, karts_dir):
    kart_dir = karts_dir / kart_name
    print(f"\nProcessing kart: {kart_name}")
    
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
    for vertex in mesh.vertices:
        # Convert Blender (Z-up, Y-forward) to STK (Y-up, Z-forward)
        bx, by, bz = vertex.co.x, vertex.co.y, vertex.co.z
        vertex.co.x = bx
        vertex.co.y = bz
        vertex.co.z = by
        
    world_positions = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    mins = [min(position[i] for position in world_positions) for i in range(3)]
    maxs = [max(position[i] for position in world_positions) for i in range(3)]
    center = tuple((mins[i] + maxs[i]) * 0.5 for i in range(3))
    
    try:
        triangulate = obj.modifiers.new("Triangulate", "TRIANGULATE")
        bpy.ops.object.modifier_apply(modifier=triangulate.name)
    except:
        pass
    
    for polygon in obj.data.polygons:
        polygon.use_smooth = True

    extent = max(maxs[i] - mins[i] for i in range(3))
    scale = 1.0 / extent if extent > 0.001 else 1.0
    
    for vertex in mesh.vertices:
        vertex.co.x = (vertex.co.x - center[0]) * scale
        vertex.co.y = (vertex.co.y - center[1]) * scale
        vertex.co.z = (vertex.co.z - center[2]) * scale
    
    world_positions = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    min_y = min(position[1] for position in world_positions)
    for vertex in mesh.vertices:
        vertex.co.y -= min_y
        
    obj.location = (0.0, 0.0, 0.0)
    mesh.update()
    
    pngs = [p for p in kart_dir.rglob("*.png") if not p.name.endswith("_icon.png")] + list(kart_dir.rglob("*.jpg"))
    tex_name = f"{kart_name}.png"
    if pngs:
        dest = kart_dir / tex_name
        if pngs[0].resolve() != dest.resolve():
            shutil.copy2(pngs[0], dest)
        tex_name = pngs[0].name if pngs[0].resolve() == dest.resolve() else tex_name
    else:
        # GLB might have embedded textures, let's extract them
        if obj.data.materials and obj.data.materials[0] and obj.data.materials[0].use_nodes:
            nodes = obj.data.materials[0].node_tree.nodes
            tex_nodes = [n for n in nodes if n.type == 'TEX_IMAGE']
            if tex_nodes and tex_nodes[0].image:
                img = tex_nodes[0].image
                img.filepath_raw = str(kart_dir / tex_name)
                img.file_format = 'PNG'
                img.save()
    
    mesh_dict = blender_object_to_mesh_dict(kart_name, obj, tex_name)
    spm_path = kart_dir / f"{kart_name}.spm"
    write_spm(spm_path, mesh_dict)
    
    mkarts_dir = Path(r"C:\Users\robso\OneDrive\Desktop\PersonalGames\TestGame\MKarts")
    graphics = list(mkarts_dir.glob(f"*{kart_name.title()}*_Graphic.png"))
    if not graphics:
        graphics = list(mkarts_dir.glob(f"*{kart_name.capitalize()}*_Graphic.png"))
    
    icon_name = f"{kart_name}_icon.png"
    if graphics:
        shutil.copy2(graphics[0], kart_dir / icon_name)
    
    kart_xml = f"""<?xml version="1.0"?>
<kart name="{kart_name.title()}"
      version="3"
      model-file="{kart_name}.spm"
      icon-file="{icon_name}"
      minimap-icon-file="{icon_name}"
      type="medium"
      groups="minkowski">
</kart>"""
    (kart_dir / "kart.xml").write_text(kart_xml, encoding="utf-8")
    
    # Remove any existing materials.xml
    if (kart_dir / "materials.xml").exists():
        (kart_dir / "materials.xml").unlink()
    
    print(f"Successfully processed kart: {kart_name}")

if __name__ == "__main__":
    karts_dir = Path(r"C:\Users\robso\OneDrive\Desktop\PersonalGames\TestGame\stk-assets\karts")
    karts_to_process = ["curie", "einstein", "feynman", "maxwell", "minkowski", "newton", "noether", "schrodinger"]
    for kart in karts_to_process:
        if (karts_dir / kart).exists():
            convert_kart(kart, karts_dir)
