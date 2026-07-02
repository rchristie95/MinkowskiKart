#ifndef HEADER_GE_METAL_SHADER_MANAGER_HPP
#define HEADER_GE_METAL_SHADER_MANAGER_HPP

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include <map>
#include <string>

namespace GE
{
class GEMetalDriver;

// Runtime MSL compiler / library cache for the native Metal backend. Mirrors
// GEVulkanShaderManager: it reads .metal source files from the shader folder and
// compiles them to MTLLibrary at load time via newLibraryWithSource:, injecting
// the same family of predefines the Vulkan path injects (PBR_ENABLED,
// SAMPLER_SIZE, TOTAL_MESH_TEXTURE_LAYER, BIND_MESH_TEXTURES_AT_ONCE, TILED_GPU,
// GE_DISABLE_DISPLACE_SSR, ...) as Metal preprocessor macros.
//
// Handles cross the ObjC/C++ boundary as void* (an id<MTLLibrary> or
// id<MTLFunction>); the caches keep the objects alive for the driver lifetime,
// so callers in .mm files bridge them back with (__bridge id<...>)handle.
namespace GEMetalShaderManager
{
// device is an id<MTLDevice> passed as void*.
void init(GEMetalDriver* driver, void* mtl_device,
          const std::string& shader_folder);
void destroy();

// The predefine block shared by every shader (built once in init()).
const std::string& getPredefines();

// Compile (or fetch cached) the MTLLibrary for a .metal file. Returns an
// id<MTLLibrary> as void*, or nullptr on failure (error already logged).
void* getLibrary(const std::string& metal_filename);

// Fetch a named function from a shader file's library, specialized with the
// given bool function-constants (by constant index). Returns an id<MTLFunction>
// as void*, cached per (file, function, constant-set). nullptr on failure.
void* getFunction(const std::string& metal_filename,
                  const std::string& function_name,
                  const std::map<unsigned, bool>& bool_constants = {});

unsigned getSamplerSize();
unsigned getMeshTextureLayer();
};   // GEMetalShaderManager

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_

#endif
