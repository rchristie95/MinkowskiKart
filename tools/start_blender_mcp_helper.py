import bpy
import addon_utils

BLEND_PATH = r"C:\Users\robso\OneDrive\Desktop\PersonalGames\TestGame\stk-assets\tracks\mobius_track\mobius_track.blend"

bpy.ops.wm.open_mainfile(filepath=BLEND_PATH)
addon_utils.enable("blender_mcp", default_set=True, persistent=True)

bpy.context.scene.blendermcp_port = 9876
bpy.context.scene.blendermcp_use_polyhaven = False
bpy.context.scene.blendermcp_use_hyper3d = False
bpy.context.scene.blendermcp_use_sketchfab = False
bpy.context.scene.blendermcp_use_hunyuan3d = False

prefs = bpy.context.preferences.addons.get("blender_mcp")
if prefs and hasattr(prefs.preferences, "telemetry_consent"):
    prefs.preferences.telemetry_consent = False

bpy.ops.blendermcp.start_server()
print("BLENDER_MCP_HELPER_READY")
