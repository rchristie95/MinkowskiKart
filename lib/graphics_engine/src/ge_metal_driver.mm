#include "ge_metal_driver.hpp"

#ifdef _IRR_COMPILE_WITH_METAL_

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "ge_metal_features.hpp"
#include "SDL_metal.h"
#include "../source/Irrlicht/os.h"

#include <stdexcept>
#include <string>

// This file is compiled with -fobjc-arc (see lib/graphics_engine/CMakeLists.txt),
// so Metal objects held in Impl are managed by ARC.

namespace GE
{
// ============================================================================
// Inline MSL for the Phase-0 clear+triangle spike. Compiled at runtime via
// newLibraryWithSource: so no offline .metallib is required yet (the offline
// metallib toolchain lands with the real shader tree in Phase 3).
static NSString* const g_spike_msl = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 position [[position]]; float3 color; };

vertex VOut spike_vertex(uint vid [[vertex_id]])
{
    // A single clip-space triangle with per-vertex colours.
    const float2 pos[3] = { float2(0.0, 0.6), float2(-0.6, -0.6), float2(0.6, -0.6) };
    const float3 col[3] = { float3(1,0,0), float3(0,1,0), float3(0,0,1) };
    VOut o;
    o.position = float4(pos[vid], 0.0, 1.0);
    o.color = col[vid];
    return o;
}

fragment float4 spike_fragment(VOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
)MSL";

// ============================================================================
struct GEMetalDriver::Impl
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
    SDL_MetalView sdl_view = nullptr;

    id<MTLRenderPipelineState> spike_pipeline = nil;

    GEMetalFeatures features;

    // Per-frame transient state, retained between beginScene and endScene.
    id<CAMetalDrawable> drawable = nil;
    id<MTLCommandBuffer> command_buffer = nil;
};

// ----------------------------------------------------------------------------
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

    // Build the spike pipeline from the inline MSL above.
    NSError* err = nil;
    id<MTLLibrary> lib = [m_impl->device newLibraryWithSource:g_spike_msl
                                                      options:nil
                                                        error:&err];
    if (lib == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error("Metal spike shader compile failed: " + msg);
    }
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [lib newFunctionWithName:@"spike_vertex"];
    pd.fragmentFunction = [lib newFunctionWithName:@"spike_fragment"];
    pd.colorAttachments[0].pixelFormat = m_impl->layer.pixelFormat;
    m_impl->spike_pipeline =
        [m_impl->device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (m_impl->spike_pipeline == nil)
    {
        std::string msg = err ? err.localizedDescription.UTF8String : "unknown";
        delete m_impl;
        m_impl = NULL;
        throw std::runtime_error("Metal spike pipeline creation failed: " + msg);
    }

    ScreenSize = params.WindowSize;
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

    @autoreleasepool
    {
        m_impl->drawable = [m_impl->layer nextDrawable];
        if (m_impl->drawable == nil)
            return false;

        m_impl->command_buffer = [m_impl->queue commandBuffer];

        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = m_impl->drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(
            color.getRed()   / 255.0,
            color.getGreen() / 255.0,
            color.getBlue()  / 255.0,
            color.getAlpha() / 255.0);

        id<MTLRenderCommandEncoder> enc =
            [m_impl->command_buffer renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:m_impl->spike_pipeline];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
    }
    return true;
}   // beginScene

// ----------------------------------------------------------------------------
bool GEMetalDriver::endScene()
{
    if (m_impl == NULL || m_impl->command_buffer == nil)
        return false;

    @autoreleasepool
    {
        if (m_impl->drawable != nil)
            [m_impl->command_buffer presentDrawable:m_impl->drawable];
        [m_impl->command_buffer commit];
    }
    m_impl->drawable = nil;
    m_impl->command_buffer = nil;
    return true;
}   // endScene

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
