#ifndef HEADER_GE_METAL_FEATURES_HPP
#define HEADER_GE_METAL_FEATURES_HPP

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

namespace GE
{
// Capability snapshot for the active Metal device. This is a plain-C++ POD so
// it can be included from ordinary .cpp translation units; it is populated from
// an id<MTLDevice> inside ge_metal_features.mm (device passed as an opaque
// void* so no Objective-C leaks into this header).
struct GEMetalFeatures
{
    // GPU family tiers (macOS). Apple7+ implies Apple Silicon with mesh shaders
    // and argument-buffer tier 2; Mac2 covers modern Intel/AMD Macs.
    bool m_apple_family = false;      // any Apple GPU family (Apple Silicon)
    bool m_apple7_or_newer = false;   // M1+ / A14+ class
    bool m_mac2_family = false;

    // Argument buffers tier 2 are required for the bindless mesh-texture array
    // (BIND_MESH_TEXTURES_AT_ONCE); tier 1 / none uses the <=16-sampler path.
    int  m_argument_buffers_tier = 0;
    bool m_bindless = false;

    // Object + mesh shader pipeline support (Metal 3, Apple7+ / recent macOS).
    // When available it is the fast path for dynamic tessellation; otherwise the
    // compute-kernel + post-tessellation-vertex path is used.
    bool m_mesh_shaders = false;

    // Read-write texture tier (>=2 needed for the r32f/rg image stores in the
    // GTAO / HiZ / IBL compute passes without format work-arounds).
    int  m_read_write_texture_tier = 0;

    // Unified memory: every Apple Silicon and modern Mac GPU is UMA, letting the
    // dynamic buffers use MTLStorageModeShared directly (no staging copy).
    bool m_unified_memory = true;

    // Metal caps samplers at 16 per shader stage; mirrors the TILED_GPU shader
    // accommodations already used for MoltenVK.
    int  m_max_samplers_per_stage = 16;

    // Human-readable device name for logging.
    char m_device_name[128] = { 0 };
};   // GEMetalFeatures

// Fill 'out' from an id<MTLDevice> (passed as void* to keep this header pure
// C++). Implemented in ge_metal_features.mm.
void geMetalPopulateFeatures(void* mtl_device, GEMetalFeatures* out);

// Log the detected features via os::Printer. Implemented in the .mm.
void geMetalPrintFeatures(const GEMetalFeatures& f);

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_

#endif
