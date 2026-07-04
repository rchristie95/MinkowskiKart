#ifndef HEADER_GE_METAL_DRIVER_HPP
#define HEADER_GE_METAL_DRIVER_HPP

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include "ge_driver.hpp"
#include "ge_metal_features.hpp"
#include "../source/Irrlicht/CNullDriver.h"
#include "SIrrCreationParameters.h"
#include "SColor.h"
#include "SDL_video.h"

#include <cstdint>
#include <vector>

using namespace irr;
using namespace video;

namespace GE
{
// Phase 0 stub of the native Metal backend. It stands up an MTLDevice, a
// CAMetalLayer obtained from the SDL window, and clears + draws a triangle each
// frame to prove the end-to-end plumbing (EDT_METAL selection, SDL Metal
// surface, drawable acquisition, command encoding, presentation).
//
// It intentionally derives directly from CNullDriver for now; Phase 1 reparents
// both this and GEVulkanDriver onto a shared GE::GEDriver interface. All
// Objective-C / Metal state is hidden behind an opaque Impl so this header stays
// includable from plain C++ translation units.
class GEMetalDriver : public GE::GEDriver
{
public:
    GEMetalDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io,
                  SDL_Window* window, IrrlichtDevice* device);

    virtual ~GEMetalDriver();

    virtual bool beginScene(bool backBuffer = true, bool zBuffer = true,
        SColor color = SColor(255, 0, 0, 0),
        const SExposedVideoData& videoData = SExposedVideoData(),
        core::rect<s32>* sourceRect = 0);

    virtual bool endScene();

    virtual E_DRIVER_TYPE getDriverType() const { return video::EDT_METAL; }

    virtual const wchar_t* getName() const { return L"GEMetalDriver (native Metal)"; }

    virtual bool queryFeature(E_VIDEO_DRIVER_FEATURE feature) const { return true; }

    virtual void setMaterial(const SMaterial& material) { Material = material; }

    // ---- 3D rendering (forward, textured, depth-tested) ---------------------
    virtual void setTransform(E_TRANSFORMATION_STATE state,
                              const core::matrix4& mat);

    virtual void drawMeshBuffer(const scene::IMeshBuffer* mb);

    virtual void drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
        const void* indexList, u32 primitiveCount, E_VERTEX_TYPE vType,
        scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType);

    virtual void OnResize(const core::dimension2d<u32>& size);

    virtual u32 getMaximalPrimitiveCount() const { return 0x7fffffff; }

    // Read back the current frame as an irrlicht image (used by STK's
    // screenshot feature / MK_AUTO_SHOT so Metal captures like the Vulkan path).
    virtual IImage* createScreenShot(
        video::ECOLOR_FORMAT format = video::ECF_UNKNOWN,
        video::E_RENDER_TARGET target = video::ERT_FRAME_BUFFER);

    // ---- 2D rendering (UI / text / HUD) --------------------------------------
    virtual void draw2DVertexPrimitiveList(const void* vertices, u32 vertexCount,
        const void* indexList, u32 primitiveCount, E_VERTEX_TYPE vType,
        scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType);

    virtual void draw2DImage(const video::ITexture* texture,
        const core::position2d<s32>& destPos, const core::rect<s32>& sourceRect,
        const core::rect<s32>* clipRect = 0,
        SColor color = SColor(255, 255, 255, 255),
        bool useAlphaChannelOfTexture = false);

    virtual void draw2DImage(const video::ITexture* texture,
        const core::rect<s32>& destRect, const core::rect<s32>& sourceRect,
        const core::rect<s32>* clipRect = 0,
        const video::SColor* const colors = 0,
        bool useAlphaChannelOfTexture = false);

    virtual void draw2DImageBatch(const video::ITexture* texture,
        const core::array<core::position2d<s32> >& positions,
        const core::array<core::rect<s32> >& sourceRects,
        const core::rect<s32>* clipRect = 0,
        SColor color = SColor(255, 255, 255, 255),
        bool useAlphaChannelOfTexture = false);

    virtual void draw2DRectangle(const core::rect<s32>& pos,
        SColor colorLeftUp, SColor colorRightUp, SColor colorLeftDown,
        SColor colorRightDown, const core::rect<s32>* clip);

    virtual void draw2DRectangle(SColor color, const core::rect<s32>& pos,
        const core::rect<s32>* clip)
    {
        draw2DRectangle(pos, color, color, color, color, clip);
    }

    // ---- Scissor / clip state read by the 2D batcher -------------------------
    virtual void enableScissorTest(const core::rect<s32>& r) { m_clip = r; }
    virtual void disableScissorTest() { m_clip = getFullscreenClip(); }
    core::rect<s32> getFullscreenClip() const
    {
        return core::rect<s32>(0, 0, ScreenSize.Width, ScreenSize.Height);
    }
    const core::rect<s32>& getCurrentClip() const { return m_clip; }
    virtual const core::dimension2d<u32>& getCurrentRenderTargetSize() const
    {
        return ScreenSize;
    }

    IrrlichtDevice* getIrrlichtDevice() const { return m_irrlicht_device; }

    // Detected capabilities of the active Metal device (valid after construction).
    const GEMetalFeatures& getMetalFeatures() const;

    // The id<MTLDevice> / id<MTLCommandQueue> as opaque void* (bridge back with
    // (__bridge id<MTL...>) in .mm translation units). Used by the resource
    // layer (textures, buffers, pipelines) to create Metal objects.
    void* getMetalDevice() const;
    void* getMetalCommandQueue() const;

private:
    struct Impl;
    Impl* m_impl;

    IrrlichtDevice* m_irrlicht_device;

    SDL_Window* m_window;

    SMaterial Material;

    core::rect<s32> m_clip;

    // Append a batch of screen-space textured triangles to the 2D queue.
    void add2DVerticesIndices(const video::S3DVertex* vertices,
        unsigned vertices_count, const uint16_t* indices,
        unsigned indices_count, const video::ITexture* texture);

    // Re-render this frame's batches into an offscreen readable texture. If
    // 'path' is set, also log stats + a luminance grid and write raw {w,h,BGRA}
    // to it. If 'out_pixels' is set, the BGRA readback is copied there.
    void dumpScreenshot(int w, int h, const char* path,
                        std::vector<uint8_t>* out_pixels = nullptr);
};   // GEMetalDriver

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_

#endif
