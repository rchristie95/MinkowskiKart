#include "ge_metal_driver.hpp"

#ifdef _IRR_COMPILE_WITH_METAL_

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include "ge_metal_features.hpp"
#include "ge_metal_texture.hpp"
#include "SDL_metal.h"
#include "../source/Irrlicht/os.h"

#include <S3DVertex.h>
#include <IMeshBuffer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Compiled with -fobjc-arc (see lib/graphics_engine/CMakeLists.txt): Metal
// objects held in Impl are ARC-managed.

namespace GE
{
// ============================================================================
// Inline MSL for the 2D (UI/text/HUD) pipeline. Positions arrive already in
// clip space; colour is uchar4 in B,G,R,A memory order (irrlicht SColor), so
// .zyxw yields RGBA, matching data/shaders/ge_shaders/2d_render.{vert,frag}.
static NSString* const g_2d_msl = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VIn
{
    float2 pos   [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv    [[attribute(2)]];
};
struct VOut
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

vertex VOut ge2d_vertex(VIn in [[stage_in]])
{
    VOut o;
    o.position = float4(in.pos, 0.0, 1.0);
    o.color = in.color.zyxw;
    o.uv = in.uv;
    return o;
}

fragment float4 ge2d_fragment(VOut in [[stage_in]],
                              texture2d<float> tex [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    return tex.sample(samp, in.uv) * in.color;
}
)MSL";

// Inline MSL for the forward 3D pipeline. Renders SPM geometry
// (S3DVertexSkinnedMesh) with a model-view-projection matrix and albedo
// texture. The GL-style projection z in [-1,1] is remapped to Metal's [0,1]
// in the shader (matching the clip matrix the GE camera bakes in).
static NSString* const g_3d_msl = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct V3In
{
    float3 pos    [[attribute(0)]];
    float4 normal [[attribute(1)]];   // Int1010102Normalized -> xyz object normal
    float4 color  [[attribute(2)]];
    half2  uv     [[attribute(3)]];
};
struct V3Out
{
    float4 position [[position]];
    float3 wnormal;
    float4 color;
    float2 uv;
};
struct U3D { float4x4 mvp; float4x4 model; };

vertex V3Out ge3d_vertex(V3In in [[stage_in]], constant U3D& u [[buffer(1)]])
{
    float4 clip = u.mvp * float4(in.pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;   // GL [-1,1] -> Metal [0,1]
    V3Out o;
    o.position = clip;
    o.wnormal = (u.model * float4(in.normal.xyz, 0.0)).xyz;
    o.color = in.color.zyxw;
    o.uv = float2(in.uv);
    return o;
}

fragment float4 ge3d_fragment(V3Out in [[stage_in]],
                             texture2d<float> tex [[texture(0)]],
                             sampler samp [[sampler(0)]])
{
    float4 t = tex.sample(samp, in.uv);
    if (t.a < 0.5) discard_fragment();               // alpha-test cutout
    float3 N = normalize(in.wnormal);
    float3 L = normalize(float3(0.35, 0.9, 0.4));     // fixed sun-ish direction
    float ndl = max(dot(N, L), 0.0);
    float light = 0.4 + 0.7 * ndl;                    // ambient + diffuse
    return float4(t.rgb * in.color.rgb * light, 1.0);
}
)MSL";

// A cached GPU copy of a static SPM mesh buffer's geometry.
struct GEMetalCachedMesh
{
    id<MTLBuffer> vbuf = nil;
    id<MTLBuffer> ibuf = nil;
    uint32_t index_count = 0;
};

struct GEMetal3DCmd
{
    id<MTLBuffer> vbuf;
    id<MTLBuffer> ibuf;
    uint32_t index_count;
    simd_float4x4 mvp;
    simd_float4x4 model;
    const video::ITexture* texture;
};

// Matches the MSL U3D constant buffer (mvp, model).
struct GEMetal3DUniforms { simd_float4x4 mvp; simd_float4x4 model; };

// Screen-space 2D vertex uploaded to the GPU (20 bytes: pos, packed BGRA, uv).
struct GEMetal2DVertex
{
    float    pos[2];
    uint32_t color;   // irrlicht SColor (0xAARRGGBB -> B,G,R,A in memory)
    float    uv[2];
};

struct GEMetal2DCmd
{
    const video::ITexture* texture;
    core::rect<s32> clip;
    uint32_t index_start;
    uint32_t index_count;
};

// ============================================================================
struct GEMetalDriver::Impl
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
    SDL_MetalView sdl_view = nullptr;

    GEMetalFeatures features;

    // 2D pipeline + resources.
    id<MTLRenderPipelineState> pipeline_2d = nil;
    id<MTLSamplerState> sampler_2d = nil;
    id<MTLBuffer> vbuf = nil;
    id<MTLBuffer> ibuf = nil;
    id<MTLTexture> white = nil;

    std::vector<GEMetal2DVertex> verts;
    std::vector<uint16_t> indices;
    std::vector<GEMetal2DCmd> cmds;

    // 3D forward pipeline + resources.
    id<MTLRenderPipelineState> pipeline_3d = nil;
    id<MTLDepthStencilState> depth_test = nil;    // 3D: less, write
    id<MTLDepthStencilState> depth_none = nil;    // 2D: always, no write
    id<MTLTexture> depth_tex = nil;
    std::map<const void*, GEMetalCachedMesh> mesh_cache;
    std::vector<GEMetal3DCmd> cmds3d;
    irr::core::matrix4 mat_world;
    irr::core::matrix4 mat_view;
    irr::core::matrix4 mat_proj;

    // Per-frame transient state.
    id<CAMetalDrawable> drawable = nil;
    id<MTLCommandBuffer> command_buffer = nil;
    MTLClearColor clear_color = MTLClearColorMake(0, 0, 0, 1);
};

// ----------------------------------------------------------------------------
// Debug offscreen readback: re-render this frame's 2D batches into a readable
// texture, log pixel stats + a coarse luminance grid, and write raw {w,h,BGRA}
// to 'path'. Enabled via GE_METAL_SCREENSHOT so the UI can be verified without
// Screen Recording permission. Runs once.
void GEMetalDriver::dumpScreenshot(int w, int h, const char* path)
{
    Impl* d = m_impl;
    if (d == nullptr || w <= 0 || h <= 0)
        return;
    @autoreleasepool
    {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:w height:h mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> off = [d->device newTextureWithDescriptor:td];

        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                         width:w height:h mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        id<MTLTexture> off_depth = [d->device newTextureWithDescriptor:dd];

        MTLRenderPassDescriptor* rp =
            [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = off;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = d->clear_color;
        rp.depthAttachment.texture = off_depth;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;

        id<MTLCommandBuffer> cb = [d->queue commandBuffer];
        id<MTLRenderCommandEncoder> enc =
            [cb renderCommandEncoderWithDescriptor:rp];
        // 3D pass (same as endScene) so the captured frame shows the world.
        if (!d->cmds3d.empty())
        {
            [enc setRenderPipelineState:d->pipeline_3d];
            [enc setDepthStencilState:d->depth_test];
            [enc setFragmentSamplerState:d->sampler_2d atIndex:0];
            [enc setFrontFacingWinding:MTLWindingClockwise];
            [enc setCullMode:MTLCullModeBack];
            for (const GEMetal3DCmd& c : d->cmds3d)
            {
                [enc setVertexBuffer:c.vbuf offset:0 atIndex:0];
                GEMetal3DUniforms u3 = { c.mvp, c.model };
                [enc setVertexBytes:&u3 length:sizeof(u3) atIndex:1];
                id<MTLTexture> t = d->white;
                const GEMetalTexture* gt =
                    dynamic_cast<const GEMetalTexture*>(c.texture);
                if (gt && gt->getMetalTexture())
                    t = (__bridge id<MTLTexture>)gt->getMetalTexture();
                [enc setFragmentTexture:t atIndex:0];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:c.index_count indexType:MTLIndexTypeUInt16
                    indexBuffer:c.ibuf indexBufferOffset:0];
            }
        }
        if (!d->cmds.empty())
        {
            [enc setRenderPipelineState:d->pipeline_2d];
            [enc setDepthStencilState:d->depth_none];
            [enc setVertexBuffer:d->vbuf offset:0 atIndex:0];
            [enc setFragmentSamplerState:d->sampler_2d atIndex:0];
            for (const GEMetal2DCmd& cmd : d->cmds)
            {
                int x0 = std::max(0, cmd.clip.UpperLeftCorner.X);
                int y0 = std::max(0, cmd.clip.UpperLeftCorner.Y);
                int x1 = std::min(w, cmd.clip.LowerRightCorner.X);
                int y1 = std::min(h, cmd.clip.LowerRightCorner.Y);
                if (x1 <= x0 || y1 <= y0) continue;
                MTLScissorRect sc; sc.x = x0; sc.y = y0;
                sc.width = x1 - x0; sc.height = y1 - y0;
                [enc setScissorRect:sc];
                id<MTLTexture> t = d->white;
                const GEMetalTexture* gt =
                    dynamic_cast<const GEMetalTexture*>(cmd.texture);
                if (gt && gt->getMetalTexture())
                    t = (__bridge id<MTLTexture>)gt->getMetalTexture();
                [enc setFragmentTexture:t atIndex:0];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:cmd.index_count indexType:MTLIndexTypeUInt16
                    indexBuffer:d->ibuf
                    indexBufferOffset:cmd.index_start * sizeof(uint16_t)];
            }
        }
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];

        std::vector<uint8_t> pixels((size_t)w * h * 4);
        [off getBytes:pixels.data() bytesPerRow:w * 4
           fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];

        // Stats: fraction of pixels differing from the clear colour.
        uint8_t cr = (uint8_t)(d->clear_color.red * 255.0);
        uint8_t cg = (uint8_t)(d->clear_color.green * 255.0);
        uint8_t cb2 = (uint8_t)(d->clear_color.blue * 255.0);
        size_t non_clear = 0;
        for (size_t i = 0; i < (size_t)w * h; i++)
        {
            uint8_t b = pixels[i * 4 + 0], g = pixels[i * 4 + 1],
                    r = pixels[i * 4 + 2];
            if (abs((int)r - cr) > 6 || abs((int)g - cg) > 6 ||
                abs((int)b - cb2) > 6)
                non_clear++;
        }
        char msg[128];
        snprintf(msg, sizeof(msg),
            "screenshot %dx%d: %.1f%% pixels differ from clear colour",
            w, h, 100.0 * non_clear / ((double)w * h));
        irr::os::Printer::log("GEMetal", msg);

        // Coarse 16x24 luminance grid (' .:-=+*#%@').
        const char* ramp = " .:-=+*#%@";
        const int gc = 24, gr = 12;
        for (int ry = 0; ry < gr; ry++)
        {
            std::string row;
            for (int rx = 0; rx < gc; rx++)
            {
                long sum = 0, cnt = 0;
                int px0 = rx * w / gc, px1 = (rx + 1) * w / gc;
                int py0 = ry * h / gr, py1 = (ry + 1) * h / gr;
                for (int yy = py0; yy < py1; yy += 4)
                    for (int xx = px0; xx < px1; xx += 4)
                    {
                        size_t i = ((size_t)yy * w + xx) * 4;
                        sum += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3;
                        cnt++;
                    }
                int lum = cnt ? (int)(sum / cnt) : 0;
                row += ramp[std::min(9, lum * 10 / 256)];
            }
            irr::os::Printer::log("GEMetal|", row.c_str());
        }

        if (path && path[0])
        {
            FILE* f = fopen(path, "wb");
            if (f)
            {
                uint32_t ww = w, hh = h;
                fwrite(&ww, 4, 1, f); fwrite(&hh, 4, 1, f);
                fwrite(pixels.data(), 1, pixels.size(), f);
                fclose(f);
            }
        }
    }
}   // geMetalDumpScreenshot

// ============================================================================
GEMetalDriver::GEMetalDriver(const SIrrlichtCreationParameters& params,
                             io::IFileSystem* io, SDL_Window* window,
                             IrrlichtDevice* device)
    : GE::GEDriver(io, params.WindowSize), m_irrlicht_device(device),
      m_window(window)
{
    m_impl = new Impl();

    m_impl->device = MTLCreateSystemDefaultDevice();
    if (m_impl->device == nil)
    {
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error("MTLCreateSystemDefaultDevice returned nil "
            "(no Metal-capable GPU?)");
    }
    m_impl->queue = [m_impl->device newCommandQueue];

    geMetalPopulateFeatures((__bridge void*)m_impl->device, &m_impl->features);
    geMetalPrintFeatures(m_impl->features);

    m_impl->sdl_view = SDL_Metal_CreateView(window);
    if (m_impl->sdl_view == nullptr)
    {
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error(std::string("SDL_Metal_CreateView failed: ") +
            SDL_GetError());
    }
    m_impl->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(m_impl->sdl_view);
    m_impl->layer.device = m_impl->device;
    m_impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m_impl->layer.framebufferOnly = YES;
    // Keep drawable pixels == screen coordinates so 2D scissor rects line up.
    m_impl->layer.drawableSize =
        CGSizeMake(params.WindowSize.Width, params.WindowSize.Height);

    // Build the 2D pipeline from the inline MSL above.
    NSError* err = nil;
    id<MTLLibrary> lib = [m_impl->device newLibraryWithSource:g_2d_msl
                                                      options:nil
                                                        error:&err];
    if (lib == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error("Metal 2D shader compile failed: " + msg);
    }

    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatUChar4Normalized;
    vd.attributes[1].offset = offsetof(GEMetal2DVertex, color);
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format = MTLVertexFormatFloat2;
    vd.attributes[2].offset = offsetof(GEMetal2DVertex, uv);
    vd.attributes[2].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(GEMetal2DVertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [lib newFunctionWithName:@"ge2d_vertex"];
    pd.fragmentFunction = [lib newFunctionWithName:@"ge2d_fragment"];
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = m_impl->layer.pixelFormat;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    // The frame render pass carries a depth attachment (for 3D), so the 2D
    // pipeline must declare a matching depth format even though it never writes.
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    m_impl->pipeline_2d =
        [m_impl->device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (m_impl->pipeline_2d == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error("Metal 2D pipeline creation failed: " + msg);
    }

    // ---- 3D forward pipeline (SPM geometry) ---------------------------------
    id<MTLLibrary> lib3d = [m_impl->device newLibraryWithSource:g_3d_msl
                                                        options:nil error:&err];
    if (lib3d == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl; m_impl = NULL;
        throw std::runtime_error("Metal 3D shader compile failed: " + msg);
    }
    // S3DVertexSkinnedMesh: pos float3 @0, normal u32 @12, color BGRA @16,
    // uv half2 @20, tangent @28, joints @32, weights @40; stride 48.
    MTLVertexDescriptor* vd3 = [[MTLVertexDescriptor alloc] init];
    vd3.attributes[0].format = MTLVertexFormatFloat3;             // position @0
    vd3.attributes[0].offset = 0;   vd3.attributes[0].bufferIndex = 0;
    vd3.attributes[1].format = MTLVertexFormatInt1010102Normalized; // normal @12
    vd3.attributes[1].offset = 12;  vd3.attributes[1].bufferIndex = 0;
    vd3.attributes[2].format = MTLVertexFormatUChar4Normalized;   // color BGRA @16
    vd3.attributes[2].offset = 16;  vd3.attributes[2].bufferIndex = 0;
    vd3.attributes[3].format = MTLVertexFormatHalf2;              // uv @20
    vd3.attributes[3].offset = 20;  vd3.attributes[3].bufferIndex = 0;
    vd3.layouts[0].stride = sizeof(irr::video::S3DVertexSkinnedMesh);
    vd3.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd3 =
        [[MTLRenderPipelineDescriptor alloc] init];
    pd3.vertexFunction = [lib3d newFunctionWithName:@"ge3d_vertex"];
    pd3.fragmentFunction = [lib3d newFunctionWithName:@"ge3d_fragment"];
    pd3.vertexDescriptor = vd3;
    pd3.colorAttachments[0].pixelFormat = m_impl->layer.pixelFormat;
    pd3.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    m_impl->pipeline_3d =
        [m_impl->device newRenderPipelineStateWithDescriptor:pd3 error:&err];
    if (m_impl->pipeline_3d == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl; m_impl = NULL;
        throw std::runtime_error("Metal 3D pipeline creation failed: " + msg);
    }

    MTLDepthStencilDescriptor* ds = [[MTLDepthStencilDescriptor alloc] init];
    ds.depthCompareFunction = MTLCompareFunctionLess;
    ds.depthWriteEnabled = YES;
    m_impl->depth_test = [m_impl->device newDepthStencilStateWithDescriptor:ds];
    MTLDepthStencilDescriptor* dn = [[MTLDepthStencilDescriptor alloc] init];
    dn.depthCompareFunction = MTLCompareFunctionAlways;
    dn.depthWriteEnabled = NO;
    m_impl->depth_none = [m_impl->device newDepthStencilStateWithDescriptor:dn];

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.mipFilter = MTLSamplerMipFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    m_impl->sampler_2d = [m_impl->device newSamplerStateWithDescriptor:sd];

    // 1x1 white texture, used when a 2D primitive has no texture bound.
    MTLTextureDescriptor* wd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:1 height:1 mipmapped:NO];
    m_impl->white = [m_impl->device newTextureWithDescriptor:wd];
    uint32_t wpix = 0xFFFFFFFF;
    [m_impl->white replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                     mipmapLevel:0
                       withBytes:&wpix
                     bytesPerRow:4];

    ScreenSize = params.WindowSize;
    m_clip = getFullscreenClip();
}   // GEMetalDriver

// ----------------------------------------------------------------------------
GEMetalDriver::~GEMetalDriver()
{
    if (m_impl == NULL)
        return;
    if (m_impl->sdl_view != nullptr)
        SDL_Metal_DestroyView(m_impl->sdl_view);
    delete m_impl;
    m_impl = NULL;
}   // ~GEMetalDriver

// ----------------------------------------------------------------------------
bool GEMetalDriver::beginScene(bool backBuffer, bool zBuffer, SColor color,
                               const SExposedVideoData& videoData,
                               core::rect<s32>* sourceRect)
{
    if (m_impl == NULL)
        return false;

    m_impl->drawable = [m_impl->layer nextDrawable];
    if (m_impl->drawable == nil)
        return false;

    m_impl->command_buffer = [m_impl->queue commandBuffer];
    m_impl->clear_color = MTLClearColorMake(
        color.getRed()   / 255.0, color.getGreen() / 255.0,
        color.getBlue()  / 255.0, color.getAlpha() / 255.0);

    m_impl->verts.clear();
    m_impl->indices.clear();
    m_impl->cmds.clear();
    m_impl->cmds3d.clear();
    m_clip = getFullscreenClip();
    return true;
}   // beginScene

// ----------------------------------------------------------------------------
void GEMetalDriver::setTransform(E_TRANSFORMATION_STATE state,
                                 const core::matrix4& mat)
{
    if (m_impl == NULL)
        return;
    switch (state)
    {
    case ETS_WORLD:      m_impl->mat_world = mat; break;
    case ETS_VIEW:       m_impl->mat_view = mat;  break;
    case ETS_PROJECTION: m_impl->mat_proj = mat;  break;
    default: break;
    }
}   // setTransform

// ----------------------------------------------------------------------------
void GEMetalDriver::drawMeshBuffer(const scene::IMeshBuffer* mb)
{
    if (m_impl == NULL || mb == NULL)
        return;
    // Only SPM geometry (the format GE meshes are converted to) is handled.
    if (mb->getVertexType() != video::EVT_SKINNED_MESH)
        return;
    const u32 vcount = mb->getVertexCount();
    const u32 icount = mb->getIndexCount();
    if (vcount == 0 || icount == 0)
        return;

    // Cache the static geometry on the GPU, keyed by the mesh-buffer pointer.
    GEMetalCachedMesh& cm = m_impl->mesh_cache[mb];
    if (cm.vbuf == nil)
    {
        const size_t vbytes =
            (size_t)vcount * sizeof(irr::video::S3DVertexSkinnedMesh);
        const size_t ibytes = (size_t)icount * sizeof(uint16_t);
        cm.vbuf = [m_impl->device newBufferWithBytes:mb->getVertices()
            length:vbytes options:MTLResourceStorageModeShared];
        cm.ibuf = [m_impl->device newBufferWithBytes:mb->getIndices()
            length:ibytes options:MTLResourceStorageModeShared];
        cm.index_count = icount;
    }

    // MVP = proj * view * world (irrlicht convention), passed column-major.
    irr::core::matrix4 mvp = m_impl->mat_proj * m_impl->mat_view *
        m_impl->mat_world;
    auto toSimd = [](const irr::core::matrix4& im) -> simd_float4x4
    {
        simd_float4x4 o;
        const float* p = im.pointer();
        for (int c = 0; c < 4; c++)
            o.columns[c] = simd_make_float4(p[c*4+0], p[c*4+1], p[c*4+2],
                p[c*4+3]);
        return o;
    };

    GEMetal3DCmd cmd;
    cmd.vbuf = cm.vbuf;
    cmd.ibuf = cm.ibuf;
    cmd.index_count = cm.index_count;
    cmd.mvp = toSimd(mvp);
    cmd.model = toSimd(m_impl->mat_world);
    cmd.texture = mb->getMaterial().getTexture(0);
    m_impl->cmds3d.push_back(cmd);
}   // drawMeshBuffer

// ----------------------------------------------------------------------------
bool GEMetalDriver::endScene()
{
    if (m_impl == NULL || m_impl->command_buffer == nil)
        return false;

    @autoreleasepool
    {
        MTLRenderPassDescriptor* rp =
            [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = m_impl->drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = m_impl->clear_color;

        // Depth buffer for the 3D pass (recreated on resize).
        NSUInteger dw = m_impl->drawable.texture.width;
        NSUInteger dh = m_impl->drawable.texture.height;
        if (m_impl->depth_tex == nil || m_impl->depth_tex.width != dw ||
            m_impl->depth_tex.height != dh)
        {
            MTLTextureDescriptor* dd = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:dw height:dh mipmapped:NO];
            dd.usage = MTLTextureUsageRenderTarget;
            dd.storageMode = MTLStorageModePrivate;
            m_impl->depth_tex = [m_impl->device newTextureWithDescriptor:dd];
        }
        rp.depthAttachment.texture = m_impl->depth_tex;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;

        id<MTLRenderCommandEncoder> enc =
            [m_impl->command_buffer renderCommandEncoderWithDescriptor:rp];

        // ---- 3D forward pass (opaque, depth-tested) -------------------------
        if (!m_impl->cmds3d.empty())
        {
            [enc setRenderPipelineState:m_impl->pipeline_3d];
            [enc setDepthStencilState:m_impl->depth_test];
            [enc setFragmentSamplerState:m_impl->sampler_2d atIndex:0];
            // GE geometry uses clockwise front faces (VK_FRONT_FACE_CLOCKWISE).
            [enc setFrontFacingWinding:MTLWindingClockwise];
            [enc setCullMode:MTLCullModeBack];
            for (const GEMetal3DCmd& c : m_impl->cmds3d)
            {
                [enc setVertexBuffer:c.vbuf offset:0 atIndex:0];
                GEMetal3DUniforms u3 = { c.mvp, c.model };
                [enc setVertexBytes:&u3 length:sizeof(u3) atIndex:1];
                id<MTLTexture> t = m_impl->white;
                const GEMetalTexture* gt =
                    dynamic_cast<const GEMetalTexture*>(c.texture);
                if (gt && gt->getMetalTexture())
                    t = (__bridge id<MTLTexture>)gt->getMetalTexture();
                [enc setFragmentTexture:t atIndex:0];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:c.index_count
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:c.ibuf
                         indexBufferOffset:0];
            }
        }

        if (!m_impl->cmds.empty())
        {
            // (Re)upload the 2D vertex/index streams for this frame.
            const size_t vbytes = m_impl->verts.size() * sizeof(GEMetal2DVertex);
            const size_t ibytes = m_impl->indices.size() * sizeof(uint16_t);
            if (m_impl->vbuf == nil || m_impl->vbuf.length < vbytes)
            {
                m_impl->vbuf = [m_impl->device
                    newBufferWithLength:(vbytes * 2 + 4096)
                                options:MTLResourceStorageModeShared];
            }
            if (m_impl->ibuf == nil || m_impl->ibuf.length < ibytes)
            {
                m_impl->ibuf = [m_impl->device
                    newBufferWithLength:(ibytes * 2 + 4096)
                                options:MTLResourceStorageModeShared];
            }
            std::memcpy(m_impl->vbuf.contents, m_impl->verts.data(), vbytes);
            std::memcpy(m_impl->ibuf.contents, m_impl->indices.data(), ibytes);

            [enc setRenderPipelineState:m_impl->pipeline_2d];
            [enc setDepthStencilState:m_impl->depth_none];
            [enc setVertexBuffer:m_impl->vbuf offset:0 atIndex:0];
            [enc setFragmentSamplerState:m_impl->sampler_2d atIndex:0];

            const int sw = (int)ScreenSize.Width;
            const int sh = (int)ScreenSize.Height;
            for (const GEMetal2DCmd& cmd : m_impl->cmds)
            {
                int x0 = std::max(0, cmd.clip.UpperLeftCorner.X);
                int y0 = std::max(0, cmd.clip.UpperLeftCorner.Y);
                int x1 = std::min(sw, cmd.clip.LowerRightCorner.X);
                int y1 = std::min(sh, cmd.clip.LowerRightCorner.Y);
                if (x1 <= x0 || y1 <= y0)
                    continue;
                MTLScissorRect sc;
                sc.x = x0; sc.y = y0;
                sc.width = x1 - x0; sc.height = y1 - y0;
                [enc setScissorRect:sc];

                id<MTLTexture> mtl_tex = m_impl->white;
                const GEMetalTexture* gt =
                    dynamic_cast<const GEMetalTexture*>(cmd.texture);
                if (gt && gt->getMetalTexture())
                    mtl_tex = (__bridge id<MTLTexture>)gt->getMetalTexture();
                [enc setFragmentTexture:mtl_tex atIndex:0];

                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd.index_count
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:m_impl->ibuf
                         indexBufferOffset:cmd.index_start * sizeof(uint16_t)];
            }
        }
        [enc endEncoding];

        [m_impl->command_buffer presentDrawable:m_impl->drawable];
        [m_impl->command_buffer commit];
    }
    m_impl->drawable = nil;
    m_impl->command_buffer = nil;

    // Offscreen verification dump (GE_METAL_SCREENSHOT=<path>). Fires once at
    // frame GE_METAL_SHOT_FRAME (default 1) so a mid-race HUD frame can be
    // captured rather than the loading screen.
    static int s_frame = 0;
    static bool s_dumped = false;
    s_frame++;
    const char* shot = getenv("GE_METAL_SCREENSHOT");
    if (shot && !s_dumped)
    {
        const char* fs = getenv("GE_METAL_SHOT_FRAME");
        int target = fs ? atoi(fs) : 1;
        if (s_frame >= target)
        {
            s_dumped = true;
            dumpScreenshot((int)ScreenSize.Width, (int)ScreenSize.Height, shot);
        }
    }
    return true;
}   // endScene

// ----------------------------------------------------------------------------
void GEMetalDriver::add2DVerticesIndices(const video::S3DVertex* vertices,
                                         unsigned vertices_count,
                                         const uint16_t* indices,
                                         unsigned indices_count,
                                         const video::ITexture* texture)
{
    if (m_impl == NULL || vertices_count == 0 || indices_count == 0)
        return;
    const uint32_t base = (uint32_t)m_impl->verts.size();
    if (base + vertices_count > 65535)
        return;

    const float w = (float)ScreenSize.Width;
    const float h = (float)ScreenSize.Height;
    for (unsigned i = 0; i < vertices_count; i++)
    {
        const video::S3DVertex& v = vertices[i];
        GEMetal2DVertex out;
        // Screen (top-left origin, y down) -> Metal clip space (y up).
        out.pos[0] = (v.Pos.X / w) * 2.0f - 1.0f;
        out.pos[1] = 1.0f - (v.Pos.Y / h) * 2.0f;
        out.color = v.Color.color;
        out.uv[0] = v.TCoords.X;
        out.uv[1] = v.TCoords.Y;
        m_impl->verts.push_back(out);
    }

    const uint32_t index_start = (uint32_t)m_impl->indices.size();
    const unsigned tri_indices = indices_count * 3;
    for (unsigned i = 0; i < tri_indices; i++)
        m_impl->indices.push_back((uint16_t)(base + indices[i]));

    GEMetal2DCmd cmd;
    cmd.texture = texture;
    cmd.clip = getCurrentClip();
    cmd.index_start = index_start;
    cmd.index_count = tri_indices;
    m_impl->cmds.push_back(cmd);
}   // add2DVerticesIndices

// ----------------------------------------------------------------------------
void GEMetalDriver::draw2DVertexPrimitiveList(const void* vertices,
    u32 vertexCount, const void* indexList, u32 primitiveCount,
    E_VERTEX_TYPE vType, scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType)
{
    const video::ITexture* texture = Material.getTexture(0);
    if (vType != EVT_STANDARD || iType != EIT_16BIT)
        return;
    const S3DVertex* v = (const S3DVertex*)vertices;
    const u16* i = (const u16*)indexList;
    if (pType == scene::EPT_TRIANGLES)
    {
        add2DVerticesIndices(v, vertexCount, i, primitiveCount, texture);
    }
    else if (pType == scene::EPT_TRIANGLE_FAN)
    {
        std::vector<uint16_t> new_idx;
        for (unsigned k = 0; k < primitiveCount; k++)
        {
            new_idx.push_back(i[0]);
            new_idx.push_back(i[k + 1]);
            new_idx.push_back(i[k + 2]);
        }
        add2DVerticesIndices(v, vertexCount, new_idx.data(), primitiveCount,
            texture);
    }
}   // draw2DVertexPrimitiveList

// ----------------------------------------------------------------------------
void GEMetalDriver::draw2DImage(const video::ITexture* tex,
    const core::position2d<s32>& destPos, const core::rect<s32>& sourceRect,
    const core::rect<s32>* clipRect, SColor color, bool useAlphaChannelOfTexture)
{
    if (!tex || !sourceRect.isValid())
        return;
    core::position2d<s32> targetPos = destPos;
    core::position2d<s32> sourcePos = sourceRect.UpperLeftCorner;
    core::dimension2d<s32> sourceSize(sourceRect.getSize());

    if (clipRect)
    {
        if (targetPos.X < clipRect->UpperLeftCorner.X)
        {
            sourceSize.Width += targetPos.X - clipRect->UpperLeftCorner.X;
            if (sourceSize.Width <= 0) return;
            sourcePos.X -= targetPos.X - clipRect->UpperLeftCorner.X;
            targetPos.X = clipRect->UpperLeftCorner.X;
        }
        if (targetPos.X + (s32)sourceSize.Width > clipRect->LowerRightCorner.X)
        {
            sourceSize.Width -= (targetPos.X + sourceSize.Width) -
                clipRect->LowerRightCorner.X;
            if (sourceSize.Width <= 0) return;
        }
        if (targetPos.Y < clipRect->UpperLeftCorner.Y)
        {
            sourceSize.Height += targetPos.Y - clipRect->UpperLeftCorner.Y;
            if (sourceSize.Height <= 0) return;
            sourcePos.Y -= targetPos.Y - clipRect->UpperLeftCorner.Y;
            targetPos.Y = clipRect->UpperLeftCorner.Y;
        }
        if (targetPos.Y + (s32)sourceSize.Height > clipRect->LowerRightCorner.Y)
        {
            sourceSize.Height -= (targetPos.Y + sourceSize.Height) -
                clipRect->LowerRightCorner.Y;
            if (sourceSize.Height <= 0) return;
        }
    }
    if (targetPos.X < 0)
    {
        sourceSize.Width += targetPos.X;
        if (sourceSize.Width <= 0) return;
        sourcePos.X -= targetPos.X;
        targetPos.X = 0;
    }
    const core::dimension2d<u32>& rt = getCurrentRenderTargetSize();
    if (targetPos.X + sourceSize.Width > (s32)rt.Width)
    {
        sourceSize.Width -= (targetPos.X + sourceSize.Width) - rt.Width;
        if (sourceSize.Width <= 0) return;
    }
    if (targetPos.Y < 0)
    {
        sourceSize.Height += targetPos.Y;
        if (sourceSize.Height <= 0) return;
        sourcePos.Y -= targetPos.Y;
        targetPos.Y = 0;
    }
    if (targetPos.Y + sourceSize.Height > (s32)rt.Height)
    {
        sourceSize.Height -= (targetPos.Y + sourceSize.Height) - rt.Height;
        if (sourceSize.Height <= 0) return;
    }

    const core::dimension2d<u32>& ts = tex->getSize();
    core::rect<f32> tc;
    tc.UpperLeftCorner.X = (f32)sourcePos.X / ts.Width;
    tc.UpperLeftCorner.Y = (f32)sourcePos.Y / ts.Height;
    tc.LowerRightCorner.X = tc.UpperLeftCorner.X + (f32)sourceSize.Width / ts.Width;
    tc.LowerRightCorner.Y = tc.UpperLeftCorner.Y + (f32)sourceSize.Height / ts.Height;

    const core::rect<s32> poss(targetPos, sourceSize);
    S3DVertex vtx[4];
    vtx[0] = S3DVertex((f32)poss.UpperLeftCorner.X, (f32)poss.UpperLeftCorner.Y,
        0, 0, 0, 0, color, tc.UpperLeftCorner.X, tc.UpperLeftCorner.Y);
    vtx[1] = S3DVertex((f32)poss.LowerRightCorner.X, (f32)poss.UpperLeftCorner.Y,
        0, 0, 0, 0, color, tc.LowerRightCorner.X, tc.UpperLeftCorner.Y);
    vtx[2] = S3DVertex((f32)poss.LowerRightCorner.X, (f32)poss.LowerRightCorner.Y,
        0, 0, 0, 0, color, tc.LowerRightCorner.X, tc.LowerRightCorner.Y);
    vtx[3] = S3DVertex((f32)poss.UpperLeftCorner.X, (f32)poss.LowerRightCorner.Y,
        0, 0, 0, 0, color, tc.UpperLeftCorner.X, tc.LowerRightCorner.Y);
    u16 idx[6] = { 0, 1, 2, 0, 2, 3 };

    if (clipRect) enableScissorTest(*clipRect);
    add2DVerticesIndices(vtx, 4, idx, 2, tex);
    if (clipRect) disableScissorTest();
}   // draw2DImage

// ----------------------------------------------------------------------------
void GEMetalDriver::draw2DImage(const video::ITexture* tex,
    const core::rect<s32>& destRect, const core::rect<s32>& sourceRect,
    const core::rect<s32>* clipRect, const video::SColor* const colors,
    bool useAlphaChannelOfTexture)
{
    if (!tex)
        return;
    const core::dimension2d<u32>& ss = tex->getSize();
    core::rect<f32> tc;
    tc.UpperLeftCorner.X = (f32)sourceRect.UpperLeftCorner.X / ss.Width;
    tc.UpperLeftCorner.Y = (f32)sourceRect.UpperLeftCorner.Y / ss.Height;
    tc.LowerRightCorner.X = (f32)sourceRect.LowerRightCorner.X / ss.Width;
    tc.LowerRightCorner.Y = (f32)sourceRect.LowerRightCorner.Y / ss.Height;

    const video::SColor temp[4] =
        { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    const video::SColor* const c = colors ? colors : temp;

    S3DVertex vtx[4];
    vtx[0] = S3DVertex((f32)destRect.UpperLeftCorner.X,
        (f32)destRect.UpperLeftCorner.Y, 0, 0, 0, 0, c[0],
        tc.UpperLeftCorner.X, tc.UpperLeftCorner.Y);
    vtx[1] = S3DVertex((f32)destRect.LowerRightCorner.X,
        (f32)destRect.UpperLeftCorner.Y, 0, 0, 0, 0, c[3],
        tc.LowerRightCorner.X, tc.UpperLeftCorner.Y);
    vtx[2] = S3DVertex((f32)destRect.LowerRightCorner.X,
        (f32)destRect.LowerRightCorner.Y, 0, 0, 0, 0, c[2],
        tc.LowerRightCorner.X, tc.LowerRightCorner.Y);
    vtx[3] = S3DVertex((f32)destRect.UpperLeftCorner.X,
        (f32)destRect.LowerRightCorner.Y, 0, 0, 0, 0, c[1],
        tc.UpperLeftCorner.X, tc.LowerRightCorner.Y);
    u16 idx[6] = { 0, 1, 2, 0, 2, 3 };

    if (clipRect) enableScissorTest(*clipRect);
    add2DVerticesIndices(vtx, 4, idx, 2, tex);
    if (clipRect) disableScissorTest();
}   // draw2DImage

// ----------------------------------------------------------------------------
void GEMetalDriver::draw2DImageBatch(const video::ITexture* tex,
    const core::array<core::position2d<s32> >& positions,
    const core::array<core::rect<s32> >& sourceRects,
    const core::rect<s32>* clipRect, SColor color, bool useAlphaChannelOfTexture)
{
    if (!tex)
        return;
    const u32 count = core::min_<u32>(positions.size(), sourceRects.size());
    const core::dimension2d<u32>& rt = getCurrentRenderTargetSize();
    const core::dimension2d<u32>& ts = tex->getSize();

    std::vector<S3DVertex> vtx;
    std::vector<uint16_t> idx;
    for (u32 i = 0; i < count; i++)
    {
        core::position2d<s32> targetPos = positions[i];
        core::position2d<s32> sourcePos = sourceRects[i].UpperLeftCorner;
        core::dimension2d<s32> sourceSize(sourceRects[i].getSize());
        if (clipRect)
        {
            if (targetPos.X < clipRect->UpperLeftCorner.X)
            {
                sourceSize.Width += targetPos.X - clipRect->UpperLeftCorner.X;
                if (sourceSize.Width <= 0) continue;
                sourcePos.X -= targetPos.X - clipRect->UpperLeftCorner.X;
                targetPos.X = clipRect->UpperLeftCorner.X;
            }
            if (targetPos.X + (s32)sourceSize.Width > clipRect->LowerRightCorner.X)
            {
                sourceSize.Width -= (targetPos.X + sourceSize.Width) -
                    clipRect->LowerRightCorner.X;
                if (sourceSize.Width <= 0) continue;
            }
            if (targetPos.Y < clipRect->UpperLeftCorner.Y)
            {
                sourceSize.Height += targetPos.Y - clipRect->UpperLeftCorner.Y;
                if (sourceSize.Height <= 0) continue;
                sourcePos.Y -= targetPos.Y - clipRect->UpperLeftCorner.Y;
                targetPos.Y = clipRect->UpperLeftCorner.Y;
            }
            if (targetPos.Y + (s32)sourceSize.Height > clipRect->LowerRightCorner.Y)
            {
                sourceSize.Height -= (targetPos.Y + sourceSize.Height) -
                    clipRect->LowerRightCorner.Y;
                if (sourceSize.Height <= 0) continue;
            }
        }
        if (targetPos.X < 0)
        {
            sourceSize.Width += targetPos.X;
            if (sourceSize.Width <= 0) continue;
            sourcePos.X -= targetPos.X;
            targetPos.X = 0;
        }
        if (targetPos.X + sourceSize.Width > (s32)rt.Width)
        {
            sourceSize.Width -= (targetPos.X + sourceSize.Width) - rt.Width;
            if (sourceSize.Width <= 0) continue;
        }
        if (targetPos.Y < 0)
        {
            sourceSize.Height += targetPos.Y;
            if (sourceSize.Height <= 0) continue;
            sourcePos.Y -= targetPos.Y;
            targetPos.Y = 0;
        }
        if (targetPos.Y + sourceSize.Height > (s32)rt.Height)
        {
            sourceSize.Height -= (targetPos.Y + sourceSize.Height) - rt.Height;
            if (sourceSize.Height <= 0) continue;
        }

        core::rect<f32> tc;
        tc.UpperLeftCorner.X = (f32)sourcePos.X / ts.Width;
        tc.UpperLeftCorner.Y = (f32)sourcePos.Y / ts.Height;
        tc.LowerRightCorner.X = tc.UpperLeftCorner.X + (f32)sourceSize.Width / ts.Width;
        tc.LowerRightCorner.Y = tc.UpperLeftCorner.Y + (f32)sourceSize.Height / ts.Height;

        const core::rect<s32> poss(targetPos, sourceSize);
        const uint16_t cur = (uint16_t)vtx.size();
        vtx.push_back(S3DVertex((f32)poss.UpperLeftCorner.X,
            (f32)poss.UpperLeftCorner.Y, 0, 0, 0, 0, color,
            tc.UpperLeftCorner.X, tc.UpperLeftCorner.Y));
        vtx.push_back(S3DVertex((f32)poss.LowerRightCorner.X,
            (f32)poss.UpperLeftCorner.Y, 0, 0, 0, 0, color,
            tc.LowerRightCorner.X, tc.UpperLeftCorner.Y));
        vtx.push_back(S3DVertex((f32)poss.LowerRightCorner.X,
            (f32)poss.LowerRightCorner.Y, 0, 0, 0, 0, color,
            tc.LowerRightCorner.X, tc.LowerRightCorner.Y));
        vtx.push_back(S3DVertex((f32)poss.UpperLeftCorner.X,
            (f32)poss.LowerRightCorner.Y, 0, 0, 0, 0, color,
            tc.UpperLeftCorner.X, tc.LowerRightCorner.Y));
        idx.push_back(cur + 0); idx.push_back(cur + 1); idx.push_back(cur + 2);
        idx.push_back(cur + 0); idx.push_back(cur + 2); idx.push_back(cur + 3);
    }
    if (!vtx.empty())
    {
        if (clipRect) enableScissorTest(*clipRect);
        add2DVerticesIndices(vtx.data(), vtx.size(), idx.data(),
            idx.size() / 3, tex);
        if (clipRect) disableScissorTest();
    }
}   // draw2DImageBatch

// ----------------------------------------------------------------------------
void GEMetalDriver::draw2DRectangle(const core::rect<s32>& position,
    SColor colorLeftUp, SColor colorRightUp, SColor colorLeftDown,
    SColor colorRightDown, const core::rect<s32>* clip)
{
    core::rect<s32> pos = position;
    if (clip)
        pos.clipAgainst(*clip);
    if (!pos.isValid())
        return;

    S3DVertex vtx[4];
    vtx[0] = S3DVertex((f32)pos.UpperLeftCorner.X, (f32)pos.UpperLeftCorner.Y,
        0, 0, 0, 0, colorLeftUp, 0, 0);
    vtx[1] = S3DVertex((f32)pos.LowerRightCorner.X, (f32)pos.UpperLeftCorner.Y,
        0, 0, 0, 0, colorRightUp, 0, 0);
    vtx[2] = S3DVertex((f32)pos.LowerRightCorner.X, (f32)pos.LowerRightCorner.Y,
        0, 0, 0, 0, colorRightDown, 0, 0);
    vtx[3] = S3DVertex((f32)pos.UpperLeftCorner.X, (f32)pos.LowerRightCorner.Y,
        0, 0, 0, 0, colorLeftDown, 0, 0);
    u16 idx[6] = { 0, 1, 2, 0, 2, 3 };
    // No texture -> the 1x1 white texture is bound automatically in endScene.
    add2DVerticesIndices(vtx, 4, idx, 2, NULL);
}   // draw2DRectangle

// ----------------------------------------------------------------------------
const GEMetalFeatures& GEMetalDriver::getMetalFeatures() const
{
    static const GEMetalFeatures s_empty;
    return m_impl ? m_impl->features : s_empty;
}   // getMetalFeatures

// ----------------------------------------------------------------------------
void* GEMetalDriver::getMetalDevice() const
{
    return m_impl ? (__bridge void*)m_impl->device : nullptr;
}   // getMetalDevice

// ----------------------------------------------------------------------------
void* GEMetalDriver::getMetalCommandQueue() const
{
    return m_impl ? (__bridge void*)m_impl->queue : nullptr;
}   // getMetalCommandQueue

// ----------------------------------------------------------------------------
void GEMetalDriver::OnResize(const core::dimension2d<u32>& size)
{
    CNullDriver::OnResize(size);
    ScreenSize = size;
    if (m_impl != NULL && m_impl->layer != nil)
        m_impl->layer.drawableSize = CGSizeMake(size.Width, size.Height);
}   // OnResize

}   // namespace GE

// ============================================================================
// The factory is declared in irr::video (see the forward declaration in
// CIrrDeviceSDL.cpp), so it must be defined there.
namespace irr
{
namespace video
{
    IVideoDriver* createMetalDriver(const SIrrlichtCreationParameters& params,
        io::IFileSystem* io, SDL_Window* win, IrrlichtDevice* device)
    {
        return new GE::GEMetalDriver(params, io, win, device);
    }   // createMetalDriver
}   // namespace video
}   // namespace irr

#endif   // _IRR_COMPILE_WITH_METAL_
