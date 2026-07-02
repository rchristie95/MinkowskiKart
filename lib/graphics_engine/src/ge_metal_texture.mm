#include "ge_metal_texture.hpp"

#ifdef _IRR_COMPILE_WITH_METAL_

#import <Metal/Metal.h>

#include "ge_main.hpp"
#include "ge_metal_driver.hpp"
#include "ge_texture.hpp"

#include <vector>

namespace GE
{
// ----------------------------------------------------------------------------
static id<MTLDevice> metalDevice()
{
    GEMetalDriver* d = getMTLDriver();
    return d ? (__bridge id<MTLDevice>)d->getMetalDevice() : nil;
}   // metalDevice

// ----------------------------------------------------------------------------
static id<MTLCommandQueue> metalQueue()
{
    GEMetalDriver* d = getMTLDriver();
    return d ? (__bridge id<MTLCommandQueue>)d->getMetalCommandQueue() : nil;
}   // metalQueue

// ----------------------------------------------------------------------------
GEMetalTexture::GEMetalTexture(const std::string& path,
                               std::function<void(video::IImage*)> image_mani)
              : video::ITexture(path.c_str()), m_image_mani(image_mani),
                m_locked_data(NULL), m_metal_texture(NULL), m_texture_size(0),
                m_driver_type(GE::getDriver()->getDriverType()),
                m_disable_reload(false), m_single_channel(false)
{
    reload();
}   // GEMetalTexture

// ----------------------------------------------------------------------------
GEMetalTexture::GEMetalTexture(video::IImage* img, const std::string& name)
              : video::ITexture(name.c_str()), m_image_mani(nullptr),
                m_locked_data(NULL), m_metal_texture(NULL), m_texture_size(0),
                m_driver_type(GE::getDriver()->getDriverType()),
                m_disable_reload(true), m_single_channel(false)
{
    if (!img)
    {
        LoadingFailed = true;
        return;
    }
    m_size = m_orig_size = img->getDimension();
    uint8_t* data = (uint8_t*)img->lock();
    upload(data);
    img->unlock();
    img->drop();
}   // GEMetalTexture

// ----------------------------------------------------------------------------
GEMetalTexture::GEMetalTexture(const std::string& name, unsigned int size,
                               bool single_channel)
              : video::ITexture(name.c_str()), m_image_mani(nullptr),
                m_locked_data(NULL), m_metal_texture(NULL), m_texture_size(0),
                m_driver_type(GE::getDriver()->getDriverType()),
                m_disable_reload(true), m_single_channel(single_channel)
{
    m_orig_size.Width = size;
    m_orig_size.Height = size;
    m_size = m_orig_size;
    std::vector<uint8_t> data(size * size * (m_single_channel ? 1 : 4), 0);
    upload(data.data());
}   // GEMetalTexture

// ----------------------------------------------------------------------------
GEMetalTexture::~GEMetalTexture()
{
    if (m_metal_texture)
    {
        // Reclaim the +1 taken by (__bridge_retained) in upload().
        CFRelease(m_metal_texture);
        m_metal_texture = NULL;
    }
    if (m_locked_data)
        delete [] m_locked_data;
}   // ~GEMetalTexture

// ----------------------------------------------------------------------------
void GEMetalTexture::reload()
{
    if (m_disable_reload)
        return;
    const core::dimension2du& max_size = getDriver()->getDriverAttributes()
        .getAttributeAsDimension2d("MAX_TEXTURE_SIZE");
    video::IImage* texture_image = getResizedImage(NamedPath.getPtr(),
        max_size, &m_orig_size);
    if (texture_image == NULL)
    {
        LoadingFailed = true;
        return;
    }
    m_size = texture_image->getDimension();
    if (m_image_mani)
        m_image_mani(texture_image);
    uint8_t* data = (uint8_t*)texture_image->lock();
    upload(data);
    texture_image->unlock();
    texture_image->drop();
}   // reload

// ----------------------------------------------------------------------------
void GEMetalTexture::upload(uint8_t* data)
{
    id<MTLDevice> device = metalDevice();
    if (device == nil)
    {
        LoadingFailed = true;
        return;
    }
    const unsigned int w = m_size.Width;
    const unsigned int h = m_size.Height;
    if (w == 0 || h == 0)
    {
        LoadingFailed = true;
        return;
    }
    const unsigned int bpp = m_single_channel ? 1 : 4;

    MTLPixelFormat fmt = m_single_channel ?
        MTLPixelFormatR8Unorm : MTLPixelFormatBGRA8Unorm;
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:fmt
                                     width:w
                                    height:h
                                 mipmapped:YES];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    // Single-channel textures back STK's font/glyph pages: sample rgb=1, a=red,
    // matching the GL_TEXTURE_SWIZZLE setup in GEGLTexture.
    if (m_single_channel)
    {
        if (@available(macOS 10.15, *))
        {
            desc.swizzle = MTLTextureSwizzleChannelsMake(
                MTLTextureSwizzleOne, MTLTextureSwizzleOne,
                MTLTextureSwizzleOne, MTLTextureSwizzleRed);
        }
    }

    id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
    if (tex == nil)
    {
        LoadingFailed = true;
        return;
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
           mipmapLevel:0
             withBytes:data
           bytesPerRow:w * bpp];

    // Generate the mip chain on the GPU.
    id<MTLCommandQueue> queue = metalQueue();
    if (queue != nil && tex.mipmapLevelCount > 1)
    {
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit generateMipmapsForTexture:tex];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
    }

    if (m_metal_texture)
        CFRelease(m_metal_texture);
    m_metal_texture = (__bridge_retained void*)tex;
    m_texture_size = w * h * bpp;
}   // upload

// ----------------------------------------------------------------------------
void* GEMetalTexture::lock(video::E_TEXTURE_LOCK_MODE mode, u32 mipmap_level)
{
    if (mode != video::ETLM_READ_ONLY)
        return NULL;
    const core::dimension2du& max_size = getDriver()->getDriverAttributes()
        .getAttributeAsDimension2d("MAX_TEXTURE_SIZE");
    video::IImage* img = getResizedImage(NamedPath.getPtr(), max_size, NULL,
        &m_size);
    if (!img)
        return NULL;
    img->setDeleteMemory(false);
    m_locked_data = (uint8_t*)img->lock();
    img->unlock();
    img->drop();
    return m_locked_data;
}   // lock

// ----------------------------------------------------------------------------
void GEMetalTexture::updateTexture(void* data, video::ECOLOR_FORMAT format,
                                   u32 w, u32 h, u32 x, u32 y)
{
    if (m_metal_texture == NULL)
        return;
    id<MTLTexture> tex = (__bridge id<MTLTexture>)m_metal_texture;

    if (m_single_channel)
    {
        if (format == video::ECF_R8)
        {
            [tex replaceRegion:MTLRegionMake2D(x, y, w, h)
                   mipmapLevel:0
                     withBytes:data
                   bytesPerRow:w];
        }
        return;
    }

    // Non-single-channel target: expand/convert into BGRA and upload.
    if (format == video::ECF_R8)
    {
        const unsigned int size = w * h;
        std::vector<uint8_t> bgra(size * 4, 255);
        uint8_t* orig = (uint8_t*)data;
        for (unsigned int i = 0; i < size; i++)
            bgra[4 * i + 3] = orig[i];
        [tex replaceRegion:MTLRegionMake2D(x, y, w, h)
               mipmapLevel:0
                 withBytes:bgra.data()
               bytesPerRow:w * 4];
    }
    else if (format == video::ECF_A8R8G8B8)
    {
        // irrlicht A8R8G8B8 is B,G,R,A in memory == Metal BGRA8Unorm; upload raw.
        [tex replaceRegion:MTLRegionMake2D(x, y, w, h)
               mipmapLevel:0
                 withBytes:data
               bytesPerRow:w * 4];
    }
}   // updateTexture

}   // namespace GE

#endif   // _IRR_COMPILE_WITH_METAL_
