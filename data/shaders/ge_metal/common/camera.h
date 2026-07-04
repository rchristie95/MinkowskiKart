// camera.h - MSL struct mirroring data/shaders/ge_shaders/utils/camera.glsl
// (Vulkan set = 1, binding = 0 CameraBuffer -> Metal [[buffer(GE_MTL_BUF_CAMERA)]]).
// Field order and types match the std140 GLSL block so the same CPU-side UBO
// bytes bind unchanged. std140 rules: vecN/matN are 16-byte aligned, mat4 is
// four float4 columns, so no explicit padding is needed beyond the two-float
// pads already present in the source.
#ifndef GE_METAL_CAMERA_H
#define GE_METAL_CAMERA_H

// GECameraBuffer is the canonical camera UBO struct and lives in the shared
// bindings header (shared/ge_metal_bindings.h). It was previously duplicated
// here with its own include guard, which caused a redefinition error whenever
// a translation unit pulled in both this header (via sun_shadow.h) and the
// shared PBR headers (handle_pbr.h -> ge_metal_bindings.h). This file now just
// forwards to the single source of truth so both trees agree byte-for-byte.
#include "../shared/ge_metal_bindings.h"

#endif // GE_METAL_CAMERA_H
