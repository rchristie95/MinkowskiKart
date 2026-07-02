#include "ge_metal_features.hpp"

#ifdef _IRR_COMPILE_WITH_METAL_

#import <Metal/Metal.h>

#include "IrrCompileConfig.h"
#include "../source/Irrlicht/os.h"

#include <cstring>
#include <string>

namespace GE
{
// ----------------------------------------------------------------------------
void geMetalPopulateFeatures(void* mtl_device, GEMetalFeatures* out)
{
    if (mtl_device == nullptr || out == nullptr)
        return;
    id<MTLDevice> device = (__bridge id<MTLDevice>)mtl_device;

    *out = GEMetalFeatures();

    // GPU family detection. supportsFamily: is available on macOS 10.15+.
    if (@available(macOS 10.15, *))
    {
        out->m_apple_family = [device supportsFamily:MTLGPUFamilyApple1];
        out->m_apple7_or_newer = [device supportsFamily:MTLGPUFamilyApple7];
        out->m_mac2_family = [device supportsFamily:MTLGPUFamilyMac2];
    }

    // Argument buffers tier -> bindless mesh textures.
    out->m_argument_buffers_tier =
        (device.argumentBuffersSupport == MTLArgumentBuffersTier2) ? 2 : 1;
    out->m_bindless = (out->m_argument_buffers_tier >= 2);

    // Read-write texture tier for compute image stores.
    switch (device.readWriteTextureSupport)
    {
    case MTLReadWriteTextureTier2: out->m_read_write_texture_tier = 2; break;
    case MTLReadWriteTextureTier1: out->m_read_write_texture_tier = 1; break;
    default:                       out->m_read_write_texture_tier = 0; break;
    }

    // Mesh shaders: Metal 3 object/mesh pipeline, Apple7+ or a Mac GPU on a
    // recent OS. Gate on the OS availability of the mesh pipeline API.
    out->m_mesh_shaders = false;
    if (@available(macOS 13.0, *))
    {
        // Apple7+ Apple Silicon and current Metal-3 Macs expose mesh shaders.
        out->m_mesh_shaders = out->m_apple7_or_newer || out->m_mac2_family;
    }

    out->m_unified_memory = device.hasUnifiedMemory;
    out->m_max_samplers_per_stage = 16;

    const char* name = device.name ? device.name.UTF8String : "unknown";
    std::strncpy(out->m_device_name, name, sizeof(out->m_device_name) - 1);
    out->m_device_name[sizeof(out->m_device_name) - 1] = '\0';
}   // geMetalPopulateFeatures

// ----------------------------------------------------------------------------
void geMetalPrintFeatures(const GEMetalFeatures& f)
{
    std::string msg = std::string("Metal device: ") + f.m_device_name;
    irr::os::Printer::log("GEMetal", msg.c_str());

    msg = std::string("Apple family: ") + (f.m_apple_family ? "yes" : "no") +
        ", Apple7+: " + (f.m_apple7_or_newer ? "yes" : "no") +
        ", Mac2: " + (f.m_mac2_family ? "yes" : "no");
    irr::os::Printer::log("GEMetal", msg.c_str());

    msg = std::string("Argument buffers tier: ") +
        std::to_string(f.m_argument_buffers_tier) +
        " (bindless: " + (f.m_bindless ? "yes" : "no") + ")" +
        ", mesh shaders: " + (f.m_mesh_shaders ? "yes" : "no") +
        ", RW texture tier: " + std::to_string(f.m_read_write_texture_tier) +
        ", unified memory: " + (f.m_unified_memory ? "yes" : "no");
    irr::os::Printer::log("GEMetal", msg.c_str());
}   // geMetalPrintFeatures

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_
