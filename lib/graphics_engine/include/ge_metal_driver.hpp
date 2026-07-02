#ifndef HEADER_GE_METAL_DRIVER_HPP
#define HEADER_GE_METAL_DRIVER_HPP

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include "ge_driver.hpp"
#include "../source/Irrlicht/CNullDriver.h"
#include "SIrrCreationParameters.h"
#include "SColor.h"
#include "SDL_video.h"

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

    virtual void setMaterial(const SMaterial& material) {}

    virtual void OnResize(const core::dimension2d<u32>& size);

    virtual u32 getMaximalPrimitiveCount() const { return 0x7fffffff; }

    IrrlichtDevice* getIrrlichtDevice() const { return m_irrlicht_device; }

private:
    struct Impl;
    Impl* m_impl;

    IrrlichtDevice* m_irrlicht_device;

    SDL_Window* m_window;
};   // GEMetalDriver

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_

#endif
