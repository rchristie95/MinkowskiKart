#include "ge_metal_shader_manager.hpp"

#ifdef _IRR_COMPILE_WITH_METAL_

#import <Metal/Metal.h>

#include "ge_main.hpp"
#include "ge_metal_driver.hpp"
#include "ge_metal_features.hpp"
#include "../source/Irrlicht/os.h"

#include <fstream>
#include <set>
#include <sstream>

namespace GE
{
namespace GEMetalShaderManager
{
// ============================================================================
namespace
{
    id<MTLDevice> g_device = nil;
    std::string   g_folder;
    std::string   g_predefines;
    unsigned      g_sampler_size = 512;
    unsigned      g_mesh_texture_layer = 2;

    // filename -> id<MTLLibrary>
    NSMutableDictionary<NSString*, id<MTLLibrary>>* g_libraries = nil;
    // "file|func|constkey" -> id<MTLFunction>
    NSMutableDictionary<NSString*, id<MTLFunction>>* g_functions = nil;

    // Recursively inline #include "..." directives (Metal's runtime source
    // compiler has no local include search path), each unique path once.
    void inlineIncludes(const std::string& path, std::set<std::string>& seen,
                        std::ostringstream& out)
    {
        if (seen.count(path))
            return;
        seen.insert(path);

        std::ifstream in(path);
        if (!in)
        {
            os::Printer::log("GEMetalShaderManager: missing include/shader",
                path.c_str(), ELL_ERROR);
            return;
        }
        std::string dir;
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos)
            dir = path.substr(0, slash + 1);

        std::string line;
        while (std::getline(in, line))
        {
            // Detect a leading (whitespace-tolerant) #include "relative/file".
            size_t p = line.find_first_not_of(" \t");
            if (p != std::string::npos && line.compare(p, 9, "#include ") == 0)
            {
                size_t q1 = line.find('"', p);
                size_t q2 = (q1 == std::string::npos) ?
                    std::string::npos : line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                {
                    std::string rel = line.substr(q1 + 1, q2 - q1 - 1);
                    inlineIncludes(dir + rel, seen, out);
                    continue;
                }
            }
            out << line << '\n';
        }
    }   // inlineIncludes
}   // anonymous namespace

// ----------------------------------------------------------------------------
void init(GEMetalDriver* /*driver*/, void* mtl_device,
          const std::string& shader_folder)
{
    g_device = (__bridge id<MTLDevice>)mtl_device;
    g_folder = shader_folder;
    if (!g_folder.empty() && g_folder.back() != '/')
        g_folder += '/';
    g_libraries = [NSMutableDictionary dictionary];
    g_functions = [NSMutableDictionary dictionary];

    GEConfig* cfg = getGEConfig();
    std::ostringstream oss;
    if (cfg->m_pbr)
    {
        oss << "#define PBR_ENABLED 1\n";
        g_mesh_texture_layer = 8;
    }
    else
        g_mesh_texture_layer = 2;
    oss << "#define SAMPLER_SIZE " << g_sampler_size << "\n";
    oss << "#define TOTAL_MESH_TEXTURE_LAYER " << g_mesh_texture_layer << "\n";
    // Native Metal is a tile-based GPU and inherits the same accommodations the
    // Vulkan path applies under TILED_GPU (16-sampler limit, no displace SSR).
    oss << "#define TILED_GPU 1\n";
    oss << "#define GE_DISABLE_DISPLACE_SSR 1\n";
    // Bindless mesh textures require argument-buffer tier 2.
    if (const GEMetalDriver* d = getMTLDriver())
    {
        (void)d;
    }
    g_predefines = oss.str();
}   // init

// ----------------------------------------------------------------------------
void destroy()
{
    g_libraries = nil;
    g_functions = nil;
    g_device = nil;
    g_folder.clear();
    g_predefines.clear();
}   // destroy

// ----------------------------------------------------------------------------
const std::string& getPredefines()
{
    return g_predefines;
}   // getPredefines

// ----------------------------------------------------------------------------
void* getLibrary(const std::string& metal_filename)
{
    if (g_device == nil)
        return nullptr;
    NSString* key = [NSString stringWithUTF8String:metal_filename.c_str()];
    id<MTLLibrary> cached = g_libraries[key];
    if (cached != nil)
        return (__bridge void*)cached;

    std::set<std::string> seen;
    std::ostringstream body;
    inlineIncludes(g_folder + metal_filename, seen, body);
    if (body.str().empty())
        return nullptr;

    std::string source = g_predefines + body.str();
    NSString* src = [NSString stringWithUTF8String:source.c_str()];

    NSError* err = nil;
    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    id<MTLLibrary> lib = [g_device newLibraryWithSource:src
                                                options:opts
                                                  error:&err];
    if (lib == nil)
    {
        std::string msg = std::string("Failed to compile ") + metal_filename +
            ": " + (err ? err.localizedDescription.UTF8String : "unknown");
        os::Printer::log("GEMetalShaderManager", msg.c_str(), ELL_ERROR);
        return nullptr;
    }
    g_libraries[key] = lib;
    return (__bridge void*)lib;
}   // getLibrary

// ----------------------------------------------------------------------------
void* getFunction(const std::string& metal_filename,
                  const std::string& function_name,
                  const std::map<unsigned, bool>& bool_constants)
{
    void* libv = getLibrary(metal_filename);
    if (libv == nullptr)
        return nullptr;
    id<MTLLibrary> lib = (__bridge id<MTLLibrary>)libv;

    std::ostringstream keyss;
    keyss << metal_filename << '|' << function_name;
    for (auto& kv : bool_constants)
        keyss << '|' << kv.first << '=' << (kv.second ? 1 : 0);
    NSString* key = [NSString stringWithUTF8String:keyss.str().c_str()];
    id<MTLFunction> cached = g_functions[key];
    if (cached != nil)
        return (__bridge void*)cached;

    NSString* fname = [NSString stringWithUTF8String:function_name.c_str()];
    id<MTLFunction> fn = nil;
    if (bool_constants.empty())
    {
        fn = [lib newFunctionWithName:fname];
    }
    else
    {
        MTLFunctionConstantValues* cv =
            [[MTLFunctionConstantValues alloc] init];
        for (auto& kv : bool_constants)
        {
            BOOL b = kv.second ? YES : NO;
            [cv setConstantValue:&b type:MTLDataTypeBool atIndex:kv.first];
        }
        NSError* err = nil;
        fn = [lib newFunctionWithName:fname constantValues:cv error:&err];
        if (fn == nil && err != nil)
        {
            os::Printer::log("GEMetalShaderManager",
                err.localizedDescription.UTF8String, ELL_ERROR);
        }
    }
    if (fn == nil)
        return nullptr;
    g_functions[key] = fn;
    return (__bridge void*)fn;
}   // getFunction

// ----------------------------------------------------------------------------
unsigned getSamplerSize()      { return g_sampler_size; }
unsigned getMeshTextureLayer() { return g_mesh_texture_layer; }

}   // namespace GEMetalShaderManager
}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_
