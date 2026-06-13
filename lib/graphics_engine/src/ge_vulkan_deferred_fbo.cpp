#include "ge_vulkan_deferred_fbo.hpp"

#include "ge_main.hpp"
#include "ge_vulkan_attachment_texture.hpp"
#include "ge_vulkan_command_loader.hpp"
#include "ge_vulkan_driver.hpp"

#include <array>
#include <exception>
#include <stdexcept>

namespace GE
{
GEVulkanDeferredFBO::GEVulkanDeferredFBO(GEVulkanDriver* vk,
                                         const core::dimension2d<u32>& size,
                                         bool swapchain_output)
                   : GEVulkanFBOTexture(vk, size,
                     false/*lazy_depth*/),
                     m_swapchain_output(swapchain_output)
{
    m_attachments = {};
    m_descriptor_layout.fill(VK_NULL_HANDLE);
    m_descriptor_pool.fill(VK_NULL_HANDLE);
    m_descriptor_set.fill(VK_NULL_HANDLE);
    for (unsigned i = GVDFT_COLOR; i <= GVDFT_NORMAL; i++)
    {
        m_attachments[i] = new GEVulkanAttachmentTexture(vk, size,
            VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    std::vector<VkFormat> hdr_formats =
    {
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_B8G8R8A8_UNORM
    };
    VkFormat hdr_format = vk->findSupportedFormat(hdr_formats,
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
    m_attachments[GVDFT_HDR] = new GEVulkanAttachmentTexture(vk, size,
        hdr_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    if (!vk->getSeparateRTTTexture() &&
        getGEConfig()->m_auto_deferred_type == GADT_DISPLACE)
    {
        std::vector<VkFormat> displace_mask_formats =
        {
            VK_FORMAT_R8G8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM
        };
        VkFormat displace_mask_format = vk->findSupportedFormat(
            displace_mask_formats, VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        m_attachments[GVDFT_DISPLACE_MASK] = new GEVulkanAttachmentTexture(vk,
            size, displace_mask_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        if (getGEConfig()->m_screen_space_reflection_type != GSSRT_DISABLED)
        {
            m_attachments[GVDFT_DISPLACE_SSR] =
                new GEVulkanAttachmentTexture(vk, size,
                VK_FORMAT_B8G8R8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        // Per-object glow silhouettes, rendered in the displace mask pass and
        // composited as a blurred outline by displace_color.frag (port of the
        // SP/OpenGL glow pass).
        m_attachments[GVDFT_GLOW] = new GEVulkanAttachmentTexture(vk, size,
            VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        VkCommandBuffer command_buffer =
            GEVulkanCommandLoader::beginSingleTimeCommands();
        m_attachments[GVDFT_DISPLACE_MASK]->transitionImageLayout(
            command_buffer, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (getAttachment<GVDFT_DISPLACE_SSR>())
        {
            getAttachment<GVDFT_DISPLACE_SSR>()->transitionImageLayout(
                command_buffer, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        m_attachments[GVDFT_GLOW]->transitionImageLayout(
            command_buffer, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        GEVulkanCommandLoader::endSingleTimeCommands(command_buffer);

        m_attachments[GVDFT_DISPLACE_COLOR] = new GEVulkanAttachmentTexture(vk,
            size, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // m_descriptor_layout[GVDFP_HDR]
    std::array<VkDescriptorSetLayoutBinding, 3> texture_layout_binding = {};
    texture_layout_binding[0].binding = 0;
    texture_layout_binding[0].descriptorCount = 1;
    texture_layout_binding[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_layout_binding[0].pImmutableSamplers = NULL;
    texture_layout_binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texture_layout_binding[1] = texture_layout_binding[0];
    texture_layout_binding[1].binding = 1;
    texture_layout_binding[2] = texture_layout_binding[0];
    texture_layout_binding[2].binding = 2;

    VkDescriptorSetLayoutCreateInfo setinfo = {};
    setinfo.flags = 0;
    setinfo.pNext = NULL;
    setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setinfo.pBindings = texture_layout_binding.data();
    setinfo.bindingCount = texture_layout_binding.size();
    if (vkCreateDescriptorSetLayout(vk->getDevice(), &setinfo,
        NULL, &m_descriptor_layout[GVDFP_HDR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed for "
            "GVDFP_HDR in GEVulkanDeferredFBO");
    }

    // m_descriptor_pool[GVDFP_HDR]
    VkDescriptorPoolSize pool_size;
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = texture_layout_binding.size();

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(vk->getDevice(), &pool_info, NULL,
        &m_descriptor_pool[GVDFP_HDR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorPool failed for "
            "GVDFP_HDR in GEVulkanDeferredFBO");
    }

    // m_descriptor_set[GVDFP_HDR]
    std::vector<VkDescriptorSetLayout> layouts(1,
        m_descriptor_layout[GVDFP_HDR]);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool[GVDFP_HDR];
    alloc_info.descriptorSetCount = layouts.size();
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(vk->getDevice(), &alloc_info,
        &m_descriptor_set[GVDFP_HDR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkAllocateDescriptorSets failed for "
            "GVDFP_HDR in GEVulkanDeferredFBO");
    }

    std::array<VkDescriptorImageInfo, 3> image_infos = {};
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[0].imageView =
        (VkImageView)m_attachments[GVDFT_COLOR]->getTextureHandler();
    image_infos[0].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[1].imageView =
        (VkImageView)m_attachments[GVDFT_NORMAL]->getTextureHandler();
    image_infos[1].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[2].imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    image_infos[2].imageView =
        (VkImageView)m_depth_texture->getTextureHandler();
    image_infos[2].sampler = m_vk->getSampler(GVS_NEAREST);

    VkWriteDescriptorSet write_descriptor_set = {};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.descriptorCount = image_infos.size();
    write_descriptor_set.pBufferInfo = 0;
    write_descriptor_set.dstSet = m_descriptor_set[GVDFP_HDR];
    write_descriptor_set.pImageInfo = image_infos.data();

    vkUpdateDescriptorSets(vk->getDevice(), 1, &write_descriptor_set, 0,
        NULL);

    initConvertColorDescriptor(vk);
    if (getAttachment<GVDFT_DISPLACE_COLOR>())
        initDisplaceDescriptor(vk);
}   // GEVulkanDeferredFBO

// ----------------------------------------------------------------------------
GEVulkanDeferredFBO::~GEVulkanDeferredFBO()
{
    for (GEVulkanAttachmentTexture* t : m_attachments)
        delete t;
    for (VkDescriptorPool pool : m_descriptor_pool)
    {
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_vk->getDevice(), pool, NULL);
    }
    for (VkDescriptorSetLayout descriptor_layout : m_descriptor_layout)
    {
        if (descriptor_layout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_vk->getDevice(), descriptor_layout,
                NULL);
        }
    }
}   // ~GEVulkanDeferredFBO

// ----------------------------------------------------------------------------
void GEVulkanDeferredFBO::initConvertColorDescriptor(GEVulkanDriver* vk)
{
    // m_descriptor_layout[GVDFP_CONVERT_COLOR]
    std::array<VkDescriptorSetLayoutBinding, 1> texture_layout_binding = {};
    texture_layout_binding[0].binding = 0;
    texture_layout_binding[0].descriptorCount = 1;
    texture_layout_binding[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_layout_binding[0].pImmutableSamplers = NULL;
    texture_layout_binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setinfo = {};
    setinfo.flags = 0;
    setinfo.pNext = NULL;
    setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setinfo.pBindings = texture_layout_binding.data();
    setinfo.bindingCount = texture_layout_binding.size();
    if (vkCreateDescriptorSetLayout(vk->getDevice(), &setinfo,
        NULL, &m_descriptor_layout[GVDFP_CONVERT_COLOR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed for "
            "GVDFP_CONVERT_COLOR in GEVulkanDeferredFBO");
    }

    // m_descriptor_pool[GVDFP_CONVERT_COLOR]
    VkDescriptorPoolSize pool_size;
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = texture_layout_binding.size();

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(vk->getDevice(), &pool_info, NULL,
        &m_descriptor_pool[GVDFP_CONVERT_COLOR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorPool failed for "
            "GVDFP_CONVERT_COLOR in GEVulkanDeferredFBO");
    }

    // m_descriptor_set[GVDFP_CONVERT_COLOR]
    std::vector<VkDescriptorSetLayout> layouts(1,
        m_descriptor_layout[GVDFP_CONVERT_COLOR]);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool[GVDFP_CONVERT_COLOR];
    alloc_info.descriptorSetCount = layouts.size();
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(vk->getDevice(), &alloc_info,
        &m_descriptor_set[GVDFP_CONVERT_COLOR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkAllocateDescriptorSets failed for "
            "GVDFP_CONVERT_COLOR in GEVulkanDeferredFBO");
    }

    std::array<VkDescriptorImageInfo, 1> image_infos = {};
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[0].imageView =
        (VkImageView)m_attachments[GVDFT_HDR]->getTextureHandler();
    image_infos[0].sampler = m_vk->getSampler(GVS_NEAREST);

    VkWriteDescriptorSet write_descriptor_set = {};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.descriptorCount = image_infos.size();
    write_descriptor_set.pBufferInfo = 0;
    write_descriptor_set.dstSet = m_descriptor_set[GVDFP_CONVERT_COLOR];
    write_descriptor_set.pImageInfo = image_infos.data();

    vkUpdateDescriptorSets(vk->getDevice(), 1, &write_descriptor_set, 0,
        NULL);
}   // initConvertColorDescriptor

// ----------------------------------------------------------------------------
void GEVulkanDeferredFBO::initDisplaceDescriptor(GEVulkanDriver* vk)
{
    // m_descriptor_layout[GVDFP_DISPLACE_COLOR]
    std::array<VkDescriptorSetLayoutBinding, 5> texture_layout_binding = {};
    texture_layout_binding[0].binding = 0;
    texture_layout_binding[0].descriptorCount = 1;
    texture_layout_binding[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_layout_binding[0].pImmutableSamplers = NULL;
    texture_layout_binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texture_layout_binding[1] = texture_layout_binding[0];
    texture_layout_binding[1].binding = 1;
    texture_layout_binding[2] = texture_layout_binding[0];
    texture_layout_binding[2].binding = 2;
    // Scene depth, sampled by the screen-space post effects (lensing
    // occlusion tests and motion blur reprojection).
    texture_layout_binding[3] = texture_layout_binding[0];
    texture_layout_binding[3].binding = 3;
    // Per-object glow silhouettes (composited by displace_color.frag).
    texture_layout_binding[4] = texture_layout_binding[0];
    texture_layout_binding[4].binding = 4;

    VkDescriptorSetLayoutCreateInfo setinfo = {};
    setinfo.flags = 0;
    setinfo.pNext = NULL;
    setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setinfo.pBindings = texture_layout_binding.data();
    setinfo.bindingCount = texture_layout_binding.size();
    if (vkCreateDescriptorSetLayout(vk->getDevice(), &setinfo,
        NULL, &m_descriptor_layout[GVDFP_DISPLACE_COLOR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed for "
            "GVDFP_DISPLACE_COLOR in GEVulkanDeferredFBO");
    }

    int hiz_multi =
        getGEConfig()->m_screen_space_reflection_type <= GSSRT_FAST ? 2 : 1;
    // m_descriptor_pool[GVDFP_DISPLACE_COLOR]
    VkDescriptorPoolSize pool_size;
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = texture_layout_binding.size() * hiz_multi;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = hiz_multi;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(vk->getDevice(), &pool_info, NULL,
        &m_descriptor_pool[GVDFP_DISPLACE_COLOR]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorPool failed for "
            "GVDFP_DISPLACE_COLOR in GEVulkanDeferredFBO");
    }

    // m_descriptor_set[GVDFP_DISPLACE_MASK + GVDFP_DISPLACE_COLOR]
    std::vector<VkDescriptorSetLayout> layouts(hiz_multi,
        m_descriptor_layout[GVDFP_DISPLACE_COLOR]);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool[GVDFP_DISPLACE_COLOR];
    alloc_info.descriptorSetCount = layouts.size();
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(vk->getDevice(), &alloc_info,
        hiz_multi == 1 ? &m_descriptor_set[GVDFP_DISPLACE_COLOR] :
        &m_descriptor_set[GVDFP_DISPLACE_MASK]) != VK_SUCCESS)
    {
        throw std::runtime_error("vkAllocateDescriptorSets failed for "
            "GVDFP_DISPLACE_MASK + GVDFP_DISPLACE_COLOR in "
            "GEVulkanDeferredFBO");
    }

    std::array<VkDescriptorImageInfo, texture_layout_binding.size()>
        image_infos = {};
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[0].imageView =
        (VkImageView)m_attachments[GVDFT_DISPLACE_MASK]->getTextureHandler();
    image_infos[0].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[1].imageView = m_attachments[GVDFT_DISPLACE_SSR] ?
        (VkImageView)m_attachments[GVDFT_DISPLACE_SSR]->getTextureHandler() :
        (VkImageView)m_vk->getTransparentTexture()->getTextureHandler();
    image_infos[1].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[2].imageView =
        (VkImageView)m_attachments[GVDFT_DISPLACE_COLOR]->getTextureHandler();
    image_infos[2].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[3].imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    image_infos[3].imageView =
        (VkImageView)m_depth_texture->getTextureHandler();
    image_infos[3].sampler = m_vk->getSampler(GVS_NEAREST);
    image_infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[4].imageView =
        (VkImageView)m_attachments[GVDFT_GLOW]->getTextureHandler();
    image_infos[4].sampler = m_vk->getSampler(GVS_NEAREST);

    VkWriteDescriptorSet write_descriptor_set = {};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.descriptorCount = image_infos.size();
    write_descriptor_set.pBufferInfo = 0;
    write_descriptor_set.dstSet = m_descriptor_set[GVDFP_DISPLACE_COLOR];
    write_descriptor_set.pImageInfo = image_infos.data();

    vkUpdateDescriptorSets(vk->getDevice(), 1, &write_descriptor_set, 0,
        NULL);

    if (hiz_multi == 1)
        return;
    image_infos[0] = image_infos[2];
    image_infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    image_infos[1].imageView =
        (VkImageView)m_depth_texture->getTextureHandler();
    image_infos[1].sampler = m_vk->getSampler(GVS_SHADOW);
    image_infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[2].imageView =
        (VkImageView)m_vk->getTransparentTexture()->getTextureHandler();
    write_descriptor_set.dstSet = m_descriptor_set[GVDFP_DISPLACE_MASK];
    image_infos[2].sampler = m_vk->getSampler(GVS_SKYBOX);
    // Glow attachment is being rendered while this set is bound; keep the
    // binding valid with a dummy texture (it is never sampled in this pass).
    image_infos[4].imageView =
        (VkImageView)m_vk->getTransparentTexture()->getTextureHandler();

    vkUpdateDescriptorSets(vk->getDevice(), 1, &write_descriptor_set, 0,
        NULL);
}   // initDisplaceDescriptor

// ----------------------------------------------------------------------------
void GEVulkanDeferredFBO::createRTT()
{
    if (!useSwapChainOutput())
        createOutputImage();

    m_rtt_render_pass.resize(GVDFP_COUNT, VK_NULL_HANDLE);
    m_rtt_frame_buffer.resize(GVDFP_COUNT, VK_NULL_HANDLE);
    auto& sciv = m_vk->getSwapChainImageViews();

    auto create_framebuffer =
        [this](unsigned id, const std::vector<VkImageView>& attachments)
        {
            VkFramebufferCreateInfo framebuffer_info = {};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = m_rtt_render_pass[id];
            framebuffer_info.attachmentCount = attachments.size();
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = m_depth_texture->getSize().Width;
            framebuffer_info.height = m_depth_texture->getSize().Height;
            framebuffer_info.layers = 1;

            if (vkCreateFramebuffer(m_vk->getDevice(), &framebuffer_info,
                NULL, &m_rtt_frame_buffer[id]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed in createRTT");
        };

    // GVDFP_GBUFFER: color, depth, normal. The attachments are stored and
    // sampled by compute AO and deferred lighting after the pass ends.
    {
        std::array<VkAttachmentDescription, 3> attachment_desc = {};
        attachment_desc[0].format =
            m_attachments[GVDFT_COLOR]->getInternalFormat();
        attachment_desc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_desc[0].finalLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment_desc[1].format = m_depth_texture->getInternalFormat();
        attachment_desc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_desc[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachment_desc[2] = attachment_desc[0];
        attachment_desc[2].format =
            m_attachments[GVDFT_NORMAL]->getInternalFormat();

        std::array<VkAttachmentReference, 2> color_references =
        {{
            { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
        }};
        VkAttachmentReference depth_reference =
            { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = color_references.size();
        subpass.pColorAttachments = color_references.data();
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 2> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask =
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask =
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = attachment_desc.size();
        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_vk->getDevice(), &render_pass_info, NULL,
            &m_rtt_render_pass[GVDFP_GBUFFER]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed for GVDFP_GBUFFER");

        create_framebuffer(GVDFP_GBUFFER,
        {
            (VkImageView)m_attachments[GVDFT_COLOR]->getTextureHandler(),
            (VkImageView)m_depth_texture->getTextureHandler(),
            (VkImageView)m_attachments[GVDFT_NORMAL]->getTextureHandler()
        });
    }

    // GVDFP_HDR: deferred lighting writes HDR while sampling stored G-buffer.
    {
        std::array<VkAttachmentDescription, 2> attachment_desc = {};
        attachment_desc[0].format =
            m_attachments[GVDFT_HDR]->getInternalFormat();
        attachment_desc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_desc[0].finalLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment_desc[1].format = m_depth_texture->getInternalFormat();
        attachment_desc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment_desc[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[1].initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachment_desc[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference hdr_reference =
            { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depth_reference =
            { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &hdr_reference;
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 2> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask =
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_SHADER_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = attachment_desc.size();
        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_vk->getDevice(), &render_pass_info, NULL,
            &m_rtt_render_pass[GVDFP_HDR]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed for GVDFP_HDR");

        create_framebuffer(GVDFP_HDR,
        {
            (VkImageView)m_attachments[GVDFT_HDR]->getTextureHandler(),
            (VkImageView)m_depth_texture->getTextureHandler()
        });
    }

    // GVDFP_CONVERT_COLOR: tonemap HDR to the scene color/displace-color
    // target, then draw ghosts and normal transparencies against the depth.
    {
        bool swapchain_output =
            useSwapChainOutput() && !getAttachment<GVDFT_DISPLACE_COLOR>();
        std::array<VkAttachmentDescription, 2> attachment_desc = {};
        attachment_desc[0].format = swapchain_output ?
            m_vk->getSwapChainImageFormat() : VK_FORMAT_B8G8R8A8_UNORM;
        attachment_desc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_desc[0].finalLayout = swapchain_output ?
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR :
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment_desc[1].format = m_depth_texture->getInternalFormat();
        attachment_desc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment_desc[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[1].initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachment_desc[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_reference =
            { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depth_reference =
            { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 2> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask =
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = attachment_desc.size();
        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_vk->getDevice(), &render_pass_info, NULL,
            &m_rtt_render_pass[GVDFP_CONVERT_COLOR]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed for GVDFP_CONVERT_COLOR");

        if (swapchain_output)
        {
            m_rtt_frame_buffer.resize(GVDFP_CONVERT_COLOR + sciv.size(),
                VK_NULL_HANDLE);
            for (unsigned i = 0; i < sciv.size(); i++)
            {
                std::vector<VkImageView> fb_attachments =
                {
                    sciv[i],
                    (VkImageView)m_depth_texture->getTextureHandler()
                };
                VkFramebufferCreateInfo framebuffer_info = {};
                framebuffer_info.sType =
                    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebuffer_info.renderPass =
                    m_rtt_render_pass[GVDFP_CONVERT_COLOR];
                framebuffer_info.attachmentCount = fb_attachments.size();
                framebuffer_info.pAttachments = fb_attachments.data();
                framebuffer_info.width = m_depth_texture->getSize().Width;
                framebuffer_info.height = m_depth_texture->getSize().Height;
                framebuffer_info.layers = 1;
                if (vkCreateFramebuffer(m_vk->getDevice(), &framebuffer_info,
                    NULL, &m_rtt_frame_buffer[GVDFP_CONVERT_COLOR + i]) !=
                    VK_SUCCESS)
                {
                    throw std::runtime_error(
                        "vkCreateFramebuffer failed for GVDFP_CONVERT_COLOR");
                }
            }
        }
        else
        {
            VkImageView color_view = getAttachment<GVDFT_DISPLACE_COLOR>() ?
                (VkImageView)getAttachment<GVDFT_DISPLACE_COLOR>()
                    ->getTextureHandler() :
                (VkImageView)getTextureHandler();
            create_framebuffer(GVDFP_CONVERT_COLOR,
            {
                color_view,
                (VkImageView)m_depth_texture->getTextureHandler()
            });
        }
    }

    if (getAttachment<GVDFT_DISPLACE_COLOR>())
        createDisplacePasses();
}   // createRTT

// ----------------------------------------------------------------------------
void GEVulkanDeferredFBO::createDisplacePasses()
{
    m_rtt_render_pass.resize(GVDFP_COUNT, VK_NULL_HANDLE);

    // m_rtt_render_pass[GVDFP_DISPLACE_MASK]
    {
        std::vector<VkAttachmentDescription> attachment_desc(1);
        attachment_desc[0].format = m_attachments[GVDFT_DISPLACE_MASK]->getInternalFormat();
        attachment_desc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment_desc[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (getAttachment<GVDFT_DISPLACE_SSR>())
        {
            attachment_desc.push_back(attachment_desc[0]);
            attachment_desc.back().format =
                getAttachment<GVDFT_DISPLACE_SSR>()->getInternalFormat();
        }
        if (getAttachment<GVDFT_GLOW>())
        {
            attachment_desc.push_back(attachment_desc[0]);
            attachment_desc.back().format =
                getAttachment<GVDFT_GLOW>()->getInternalFormat();
        }
        VkAttachmentDescription depth_desc = {};
        depth_desc.format = m_depth_texture->getInternalFormat();
        depth_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depth_desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachment_desc.push_back(depth_desc);

        VkAttachmentReference depth_reference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
        std::vector<VkAttachmentReference> color_references =
            {{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }};
        if (getAttachment<GVDFT_DISPLACE_SSR>())
        {
            color_references.push_back(
                { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        }
        if (getAttachment<GVDFT_GLOW>())
        {
            color_references.push_back(
                { (uint32_t)color_references.size(),
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        }
        depth_reference.attachment = (uint32_t)color_references.size();

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = color_references.size();
        subpass.pColorAttachments = color_references.data();
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 1> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = attachment_desc.size();
        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_vk->getDevice(), &render_pass_info, NULL,
            &m_rtt_render_pass[GVDFP_DISPLACE_MASK]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed for GVDFP_DISPLACE_MASK");
    }

    // m_rtt_render_pass[GVDFP_DISPLACE_COLOR]
    {
        std::array<VkAttachmentDescription, 2> attachment_desc = {};
        attachment_desc[0].format = useSwapChainOutput() ?
            m_vk->getSwapChainImageFormat() : VK_FORMAT_B8G8R8A8_UNORM;
        attachment_desc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_desc[0].finalLayout = useSwapChainOutput() ?
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment_desc[1].format = m_depth_texture->getInternalFormat();
        attachment_desc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment_desc[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachment_desc[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depth_reference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 1> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = attachment_desc.size();
        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_vk->getDevice(), &render_pass_info, NULL,
            &m_rtt_render_pass[GVDFP_DISPLACE_COLOR]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed for GVDFP_DISPLACE_COLOR");
    }

    m_rtt_frame_buffer.resize(GVDFP_COUNT, VK_NULL_HANDLE);
    auto& sciv = m_vk->getSwapChainImageViews();
    if (useSwapChainOutput())
    {
        for (unsigned i = 0; i < sciv.size() - 1; i++)
            m_rtt_frame_buffer.push_back(VK_NULL_HANDLE);
    }

    // m_rtt_frame_buffer[GVDFP_DISPLACE_MASK]
    {
        std::vector<VkImageView> attachments =
        {
            (VkImageView)m_attachments[GVDFT_DISPLACE_MASK]->getTextureHandler()
        };
        if (getAttachment<GVDFT_DISPLACE_SSR>())
        {
            attachments.push_back((VkImageView)
                m_attachments[GVDFT_DISPLACE_SSR]->getTextureHandler());
        }
        if (getAttachment<GVDFT_GLOW>())
        {
            attachments.push_back((VkImageView)
                m_attachments[GVDFT_GLOW]->getTextureHandler());
        }
        attachments.push_back((VkImageView)m_depth_texture->getTextureHandler());

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = m_rtt_render_pass[GVDFP_DISPLACE_MASK];
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = m_depth_texture->getSize().Width;
        framebuffer_info.height = m_depth_texture->getSize().Height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(m_vk->getDevice(), &framebuffer_info, NULL,
            &m_rtt_frame_buffer[GVDFP_DISPLACE_MASK]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer failed for GVDFP_DISPLACE_MASK");
    }

    // m_rtt_frame_buffer[GVDFP_DISPLACE_COLOR]
    for (unsigned i = GVDFP_DISPLACE_COLOR; i < m_rtt_frame_buffer.size(); i++)
    {
        std::array<VkImageView, 2> attachments =
        {{
            useSwapChainOutput() ? sciv[i - GVDFP_DISPLACE_COLOR] : (VkImageView)getTextureHandler(),
            (VkImageView)m_depth_texture->getTextureHandler()
        }};

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = m_rtt_render_pass[GVDFP_DISPLACE_COLOR];
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = m_depth_texture->getSize().Width;
        framebuffer_info.height = m_depth_texture->getSize().Height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(m_vk->getDevice(), &framebuffer_info, NULL,
            &m_rtt_frame_buffer[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer failed for GVDFP_DISPLACE_COLOR");
    }
}   // createDisplacePasses

}
