// global_light_data.h - MSL struct mirroring
// data/shaders/ge_shaders/utils/global_light_data.glsl
// (Vulkan set = 1, binding = 3 GlobalLightBuffer ->
// Metal [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]]). Field order/types match the
// std140 GLSL block. In std140 a struct array element is padded to a 16-byte
// multiple; GELightData is 3 * float4 = 48 bytes = already a multiple of 16, so
// the array layout matches the GLSL side with no extra padding.
#ifndef GE_METAL_GLOBAL_LIGHT_DATA_H
#define GE_METAL_GLOBAL_LIGHT_DATA_H

// GELightData / GEGlobalLightBuffer are the canonical global-light UBO structs
// and live in the shared bindings header (shared/ge_metal_bindings.h). They were
// previously duplicated here with their own include guard, which caused a
// redefinition error whenever a translation unit pulled in both this header
// (via sun_shadow.h) and the shared PBR headers (pbr_light.h ->
// ge_metal_bindings.h). This file now forwards to the single source of truth so
// both trees agree byte-for-byte (shared uses packed_float3 for the vec3 fields,
// which matches std140 vec3+float packing and reads the same CPU-side bytes).
#include "../shared/ge_metal_bindings.h"

#endif // GE_METAL_GLOBAL_LIGHT_DATA_H
