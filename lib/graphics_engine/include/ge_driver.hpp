#ifndef HEADER_GE_DRIVER_HPP
#define HEADER_GE_DRIVER_HPP

#include "../source/Irrlicht/CNullDriver.h"

namespace GE
{
// Common base for the GE rendering backends: GEVulkanDriver (Vulkan/MoltenVK)
// and GEMetalDriver (native Metal). For now it is a thin marker so engine and
// game code can ask "is a GE backend active?" via GE::getGEDriver() without
// caring which one it is. Later phases hoist the backend-agnostic service
// surface (frame pacing, mesh texture descriptor, shader reload, draw-call
// cache, ...) up here as pure virtuals so the shared subsystems can target the
// interface instead of a concrete driver.
class GEDriver : public irr::video::CNullDriver
{
public:
    GEDriver(irr::io::IFileSystem* io,
             const irr::core::dimension2d<irr::u32>& screen_size)
        : irr::video::CNullDriver(io, screen_size) {}

    virtual ~GEDriver() {}
};   // GEDriver

}   // namespace GE

#endif
