#ifndef HEADER_GE_METAL_TEXTURE_HPP
#define HEADER_GE_METAL_TEXTURE_HPP

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include <functional>
#include <string>
#include <ITexture.h>

using namespace irr;

namespace GE
{
// MTLTexture-backed irrlicht ITexture for the native Metal backend, modelled on
// GEGLTexture. Kept a pure-C++ header (the id<MTLTexture> is held as an opaque,
// bridge-retained void*) so plain .cpp units such as ge_texture.cpp can build it.
class GEMetalTexture : public video::ITexture
{
private:
    core::dimension2d<u32> m_size, m_orig_size;

    std::function<void(video::IImage*)> m_image_mani;

    uint8_t* m_locked_data;

    // id<MTLTexture>, retained via (__bridge_retained); released in the dtor.
    void* m_metal_texture;

    unsigned int m_texture_size;

    const video::E_DRIVER_TYPE m_driver_type;

    const bool m_disable_reload;

    bool m_single_channel;

    // Create/replace the MTLTexture from BGRA (or single-channel) pixel data.
    void upload(uint8_t* data);

public:
    GEMetalTexture(const std::string& path,
        std::function<void(video::IImage*)> image_mani = nullptr);
    GEMetalTexture(video::IImage* img, const std::string& name);
    GEMetalTexture(const std::string& name, unsigned int size,
                   bool single_channel);
    virtual ~GEMetalTexture();

    virtual void* lock(video::E_TEXTURE_LOCK_MODE mode = video::ETLM_READ_WRITE,
                       u32 mipmap_level = 0);
    virtual void unlock()
    {
        if (m_locked_data)
        {
            delete [] m_locked_data;
            m_locked_data = NULL;
        }
    }
    virtual const core::dimension2d<u32>& getOriginalSize() const
                                                        { return m_orig_size; }
    virtual const core::dimension2d<u32>& getSize() const    { return m_size; }
    virtual video::E_DRIVER_TYPE getDriverType() const
                                                      { return m_driver_type; }
    virtual video::ECOLOR_FORMAT getColorFormat() const
                                                { return video::ECF_A8R8G8B8; }
    virtual u32 getPitch() const                                  { return 0; }
    virtual bool hasMipMaps() const                            { return true; }
    virtual void regenerateMipMapLevels(void* mipmap_data = NULL)            {}
    virtual u64 getTextureHandler() const
                                     { return (u64)(uintptr_t)m_metal_texture; }
    virtual unsigned int getTextureSize() const      { return m_texture_size; }
    virtual void reload();
    virtual void updateTexture(void* data, irr::video::ECOLOR_FORMAT format,
                               u32 w, u32 h, u32 x, u32 y);

    // The underlying id<MTLTexture> as void*; the 2D renderer / draw call bind
    // it by casting an ITexture* to GEMetalTexture*.
    void* getMetalTexture() const                    { return m_metal_texture; }
};   // GEMetalTexture

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_

#endif
