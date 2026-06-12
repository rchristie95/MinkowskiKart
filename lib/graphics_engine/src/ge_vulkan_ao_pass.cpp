#include "ge_vulkan_ao_pass.hpp"

#include "ge_main.hpp"
#include "ge_vulkan_array_texture.hpp"
#include "ge_vulkan_attachment_texture.hpp"
#include "ge_vulkan_camera_scene_node.hpp"
#include "ge_vulkan_deferred_fbo.hpp"
#include "ge_vulkan_driver.hpp"
#include "ge_vulkan_shader_manager.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

namespace GE
{
// ----------------------------------------------------------------------------
GEVulkanAOPass::GEVulkanAOPass(GEVulkanDriver* vk)
              : m_vk(vk), m_ao_raw(NULL), m_ao_result(NULL),
                m_descriptor_layout(VK_NULL_HANDLE),
                m_pipeline_layout(VK_NULL_HANDLE),
                m_ao_pipeline(VK_NULL_HANDLE),
                m_blur_pipeline(VK_NULL_HANDLE),
                m_descriptor_pool(VK_NULL_HANDLE),
                m_ao_set(VK_NULL_HANDLE), m_blur_set(VK_NULL_HANDLE)
{
}   // GEVulkanAOPass

// ----------------------------------------------------------------------------
GEVulkanAOPass::~GEVulkanAOPass()
{
    destroy();
}   // ~GEVulkanAOPass

// ----------------------------------------------------------------------------
bool GEVulkanAOPass::prepare(GEVulkanCameraSceneNode* cam)
{
    irr::core::recti viewport(irr::core::position2di(
        cam->getUBOData()->m_viewport.UpperLeftCorner.X,
        cam->getUBOData()->m_viewport.UpperLeftCorner.Y),
        irr::core::dimension2du(
        cam->getUBOData()->m_viewport.LowerRightCorner.X,
        cam->getUBOData()->m_viewport.LowerRightCorner.Y));
    m_inverse_projection = cam->getUBOData()->m_inverse_projection_matrix;
    m_screen_size = irr::core::dimension2df(
        cam->getUBOData()->m_screensize.UpperLeftCorner.X,
        cam->getUBOData()->m_screensize.UpperLeftCorner.Y);
    if (m_viewport != viewport)
    {
        m_viewport = viewport;
        destroy();
        init();
        return true;
    }
    return false;
}   // prepare

// ----------------------------------------------------------------------------
void GEVulkanAOPass::init()
{
    irr::core::dimension2du half_size(
        std::max(m_viewport.getWidth() / 2, 1),
        std::max(m_viewport.getHeight() / 2, 1));
    // R32F: core-guaranteed storage image format
    m_ao_raw = new GEVulkanArrayTexture(VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_VIEW_TYPE_2D, half_size, 1, irr::video::SColor(0),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_ao_result = new GEVulkanArrayTexture(VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_VIEW_TYPE_2D, half_size, 1, irr::video::SColor(0),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Layout: 0 = sampled input (depth or raw AO), 1 = sampled depth,
    // 2 = storage output
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1] = bindings[0];
    bindings[1].binding = 1;
    bindings[2].binding = 2;
    bindings[2].descriptorCount = 1;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();

    VkDevice device = m_vk->getDevice();
    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL,
        &m_descriptor_layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout for "
            "AO pass");
    }

    VkPushConstantRange push_constant = {};
    push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant.offset = 0;
    // mat4 inverse projection + vec4 viewport + vec4 screen size
    push_constant.size = sizeof(float) * 24;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &m_descriptor_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant;

    if (vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
        &m_pipeline_layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout for AO");

    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = GEVulkanShaderManager::getShader("ao.comp");
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = m_pipeline_layout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
        NULL, &m_ao_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create AO compute pipeline");

    pipeline_info.stage.module =
        GEVulkanShaderManager::getShader("ao_blur.comp");
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
        NULL, &m_blur_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create AO blur pipeline");

    std::array<VkDescriptorPoolSize, 2> pool_sizes = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = 4;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 2;
    pool_info.poolSizeCount = pool_sizes.size();
    pool_info.pPoolSizes = pool_sizes.data();
    if (vkCreateDescriptorPool(device, &pool_info, NULL,
        &m_descriptor_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool for AO");

    std::array<VkDescriptorSetLayout, 2> layouts =
        {{ m_descriptor_layout, m_descriptor_layout }};
    std::array<VkDescriptorSet, 2> sets = {};
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = layouts.size();
    alloc_info.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &alloc_info, sets.data()) !=
        VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets for AO");
    m_ao_set = sets[0];
    m_blur_set = sets[1];

    GEVulkanDeferredFBO* dfbo =
        static_cast<GEVulkanDeferredFBO*>(m_vk->getRTTTexture());
    VkDescriptorImageInfo depth_info = {};
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth_info.imageView =
        (VkImageView)dfbo->getDepthTexture()->getTextureHandler();
    depth_info.sampler = m_vk->getSampler(GVS_NEAREST);

    VkDescriptorImageInfo raw_sampled_info = {};
    raw_sampled_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    raw_sampled_info.imageView =
        (VkImageView)m_ao_raw->getTextureHandler();
    raw_sampled_info.sampler = m_vk->getSampler(GVS_NEAREST);

    VkDescriptorImageInfo raw_storage_info = {};
    raw_storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    raw_storage_info.imageView = (VkImageView)m_ao_raw->getTextureHandler();

    VkDescriptorImageInfo result_storage_info = {};
    result_storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    result_storage_info.imageView =
        (VkImageView)m_ao_result->getTextureHandler();

    auto write = [](VkDescriptorSet set, uint32_t binding,
                    VkDescriptorType type, VkDescriptorImageInfo* info)
    {
        VkWriteDescriptorSet ds = {};
        ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ds.dstSet = set;
        ds.dstBinding = binding;
        ds.descriptorType = type;
        ds.descriptorCount = 1;
        ds.pImageInfo = info;
        return ds;
    };
    std::array<VkWriteDescriptorSet, 6> writes =
    {{
        // AO pass: 0 = depth, 1 = depth (unused slot kept valid),
        // 2 = raw AO out
        write(m_ao_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &depth_info),
        write(m_ao_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &depth_info),
        write(m_ao_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            &raw_storage_info),
        // Blur pass: 0 = raw AO, 1 = depth, 2 = result out
        write(m_blur_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &raw_sampled_info),
        write(m_blur_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &depth_info),
        write(m_blur_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            &result_storage_info)
    }};
    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, NULL);
}   // init

// ----------------------------------------------------------------------------
void GEVulkanAOPass::destroy()
{
    VkDevice device = m_vk->getDevice();
    delete m_ao_raw;
    m_ao_raw = NULL;
    delete m_ao_result;
    m_ao_result = NULL;
    if (m_ao_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_ao_pipeline, NULL);
    m_ao_pipeline = VK_NULL_HANDLE;
    if (m_blur_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_blur_pipeline, NULL);
    m_blur_pipeline = VK_NULL_HANDLE;
    if (m_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_pipeline_layout, NULL);
    m_pipeline_layout = VK_NULL_HANDLE;
    if (m_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, m_descriptor_pool, NULL);
    m_descriptor_pool = VK_NULL_HANDLE;
    m_ao_set = VK_NULL_HANDLE;
    m_blur_set = VK_NULL_HANDLE;
    if (m_descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, m_descriptor_layout, NULL);
    m_descriptor_layout = VK_NULL_HANDLE;
}   // destroy

// ----------------------------------------------------------------------------
void GEVulkanAOPass::generate(VkCommandBuffer cmd)
{
    if (m_ao_pipeline == VK_NULL_HANDLE)
        return;

    const uint32_t w = m_ao_raw->getSize().Width;
    const uint32_t h = m_ao_raw->getSize().Height;

    struct PushConstants
    {
        float m_inverse_projection[16];
        float m_viewport[4];
        float m_screen[4];
    } pc;
    memcpy(pc.m_inverse_projection, m_inverse_projection.pointer(),
        sizeof(float) * 16);
    pc.m_viewport[0] = (float)m_viewport.UpperLeftCorner.X;
    pc.m_viewport[1] = (float)m_viewport.UpperLeftCorner.Y;
    pc.m_viewport[2] = (float)m_viewport.getWidth();
    pc.m_viewport[3] = (float)m_viewport.getHeight();
    pc.m_screen[0] = m_screen_size.Width;
    pc.m_screen[1] = m_screen_size.Height;
    pc.m_screen[2] = 0.0f;
    pc.m_screen[3] = 0.0f;

    // Depth writes (late fragment tests) -> compute sampling; the render
    // pass external dependency only covers fragment-stage readers.
    VkMemoryBarrier depth_barrier = {};
    depth_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    depth_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depth_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &depth_barrier, 0, NULL,
        0, NULL);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    // Raw AO image -> GENERAL for the estimator write (content is fully
    // rewritten each frame, so the previous layout doesn't matter)
    barrier.image = m_ao_raw->getImage();
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &barrier);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ao_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &m_ao_set, 0, NULL);
    vkCmdPushConstants(cmd, m_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(PushConstants), &pc);
    vkCmdDispatch(cmd, (w + 15) / 16, (h + 15) / 16, 1);

    // Raw AO write -> blur read; result image -> GENERAL
    barrier.image = m_ao_raw->getImage();
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &barrier);
    barrier.image = m_ao_result->getImage();
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &barrier);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_blur_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &m_blur_set, 0, NULL);
    vkCmdPushConstants(cmd, m_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(PushConstants), &pc);
    vkCmdDispatch(cmd, (w + 15) / 16, (h + 15) / 16, 1);

    // Result -> sampled by displace_color.frag
    barrier.image = m_ao_result->getImage();
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &barrier);
}   // generate

}
