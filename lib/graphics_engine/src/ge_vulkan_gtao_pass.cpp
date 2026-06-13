#include "ge_vulkan_gtao_pass.hpp"

#include "ge_main.hpp"
#include "ge_vulkan_attachment_texture.hpp"
#include "ge_vulkan_camera_scene_node.hpp"
#include "ge_vulkan_deferred_fbo.hpp"
#include "ge_vulkan_driver.hpp"
#include "ge_vulkan_dynamic_buffer.hpp"
#include "ge_vulkan_shader_manager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace GE
{
namespace
{
enum GTAOLayoutId
{
    GL_LINEAR_DEPTH = 0,
    GL_RAW_AO,
    GL_DENOISED_AO,
    GL_RESULT_AO,
    GL_HISTORY_0,
    GL_HISTORY_1,
    GL_COUNT
};

struct GTAOConstants
{
    float m_inverse_projection[16];
    float m_inverse_view[16];
    float m_projection_view[16];
    float m_previous_projection_view[16];
    float m_viewport[4];
    float m_screen[4];
    float m_params0[4]; // radius, intensity, temporal blend, frame index
    float m_params1[4]; // half width, half height, reset history, unused
};

static void copyMatrix(float* dst, const irr::core::matrix4& src)
{
    memcpy(dst, src.pointer(), sizeof(float) * 16);
}

}   // namespace

// ----------------------------------------------------------------------------
GEVulkanGTAOPass::GEVulkanGTAOPass(GEVulkanDriver* vk)
                : m_vk(vk), m_linear_depth(NULL), m_raw_ao(NULL),
                  m_denoised_ao(NULL), m_ao_result(NULL),
                  m_history({{ NULL, NULL }}), m_constants_buffer(NULL),
                  m_ao_format(VK_FORMAT_R32_SFLOAT), m_use_r16(false),
                  m_active(false), m_reset_history(true),
                  m_has_last_camera(false), m_history_index(0),
                  m_frame_index(0),
                  m_descriptor_layout(VK_NULL_HANDLE),
                  m_pipeline_layout(VK_NULL_HANDLE),
                  m_prefilter_pipeline(VK_NULL_HANDLE),
                  m_main_pipeline(VK_NULL_HANDLE),
                  m_denoise_pipeline(VK_NULL_HANDLE),
                  m_upsample_pipeline(VK_NULL_HANDLE),
                  m_descriptor_pool(VK_NULL_HANDLE)
{
    m_layouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);
}   // GEVulkanGTAOPass

// ----------------------------------------------------------------------------
GEVulkanGTAOPass::~GEVulkanGTAOPass()
{
    destroy();
}   // ~GEVulkanGTAOPass

// ----------------------------------------------------------------------------
bool GEVulkanGTAOPass::prepare(GEVulkanCameraSceneNode* cam)
{
    const bool should_enable = getGEConfig()->m_ssao;
    if (!should_enable)
    {
        const bool changed = m_active;
        m_active = false;
        return changed;
    }

    const GEVulkanCameraUBO* ubo = cam->getUBOData();
    irr::core::recti viewport(irr::core::position2di(
        ubo->m_viewport.UpperLeftCorner.X,
        ubo->m_viewport.UpperLeftCorner.Y),
        irr::core::dimension2du(
        ubo->m_viewport.LowerRightCorner.X,
        ubo->m_viewport.LowerRightCorner.Y));
    m_screen_size = irr::core::dimension2df(
        ubo->m_screensize.UpperLeftCorner.X,
        ubo->m_screensize.UpperLeftCorner.Y);
    m_inverse_projection = ubo->m_inverse_projection_matrix;
    m_inverse_view = ubo->m_inverse_view_matrix;
    m_projection_view = ubo->m_projection_view_matrix;
    m_previous_projection_view = ubo->m_previous_pv_matrix;

    bool descriptors_changed = !m_active;
    m_active = true;
    if (descriptors_changed)
        m_reset_history = true;

    if (m_viewport != viewport || !m_ao_result)
    {
        m_viewport = viewport;
        destroy();
        init();
        m_reset_history = true;
        m_has_last_camera = false;
        return true;
    }

    irr::core::vector3df dir = cam->getTarget() - cam->getAbsolutePosition();
    if (dir.getLengthSQ() > 0.0001f)
        dir.normalize();
    const bool camera_cut = m_has_last_camera &&
        (cam->getAbsolutePosition().getDistanceFromSQ(
            m_last_camera_position) > 625.0f ||
        dir.dotProduct(m_last_camera_direction) < 0.45f);
    if (camera_cut)
        m_reset_history = true;
    m_last_camera_position = cam->getAbsolutePosition();
    m_last_camera_direction = dir;
    m_has_last_camera = true;

    return descriptors_changed;
}   // prepare

// ----------------------------------------------------------------------------
void GEVulkanGTAOPass::init()
{
    const unsigned viewport_width = std::max(1, m_viewport.getWidth());
    const unsigned viewport_height = std::max(1, m_viewport.getHeight());
    irr::core::dimension2du half_size(
        std::max((viewport_width + 1) / 2, 4u),
        std::max((viewport_height + 1) / 2, 4u));
    irr::core::dimension2du full_size(
        std::max(viewport_width, 4u),
        std::max(viewport_height, 4u));

    m_ao_format = m_vk->findSupportedFormat(
        { VK_FORMAT_R16_SFLOAT, VK_FORMAT_R32_SFLOAT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    m_use_r16 = m_ao_format == VK_FORMAT_R16_SFLOAT;

    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_linear_depth = new GEVulkanAttachmentTexture(m_vk, half_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    m_raw_ao = new GEVulkanAttachmentTexture(m_vk, half_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    m_denoised_ao = new GEVulkanAttachmentTexture(m_vk, half_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    m_ao_result = new GEVulkanAttachmentTexture(m_vk, full_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    m_history[0] = new GEVulkanAttachmentTexture(m_vk, full_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    m_history[1] = new GEVulkanAttachmentTexture(m_vk, full_size,
        m_ao_format, usage, VK_IMAGE_ASPECT_COLOR_BIT);

    m_constants_buffer = new GEVulkanDynamicBuffer(
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GTAOConstants),
        m_vk->getMaxFrameInFlight(), 0);

    std::array<VkDescriptorSetLayoutBinding, 8> bindings = {};
    for (unsigned i = 0; i <= 4; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    for (unsigned i = 5; i <= 6; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[7].binding = 7;
    bindings[7].descriptorCount = 1;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();
    VkDevice device = m_vk->getDevice();
    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL,
        &m_descriptor_layout) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create descriptor set layout for GTAO");
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &m_descriptor_layout;
    if (vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
        &m_pipeline_layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout for GTAO");
    }

    auto create_pipeline = [&](const std::string& base_name)->VkPipeline
    {
        const std::string shader_name = base_name +
            (m_use_r16 ? ".comp" : "_r32.comp");
        VkComputePipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module =
            GEVulkanShaderManager::getShader(shader_name);
        pipeline_info.stage.pName = "main";
        pipeline_info.layout = m_pipeline_layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            &pipeline_info, NULL, &pipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create " + shader_name);
        }
        return pipeline;
    };

    m_prefilter_pipeline = create_pipeline("gtao_prefilter_depth");
    m_main_pipeline = create_pipeline("gtao_main");
    m_denoise_pipeline = create_pipeline("gtao_denoise");
    m_upsample_pipeline = create_pipeline("gtao_upsample");

    const uint32_t frame_count = m_vk->getMaxFrameInFlight();
    std::array<VkDescriptorPoolSize, 3> pool_sizes = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = GDS_COUNT * frame_count * 5;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[1].descriptorCount = GDS_COUNT * frame_count * 2;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[2].descriptorCount = GDS_COUNT * frame_count;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = GDS_COUNT * frame_count;
    pool_info.poolSizeCount = pool_sizes.size();
    pool_info.pPoolSizes = pool_sizes.data();
    if (vkCreateDescriptorPool(device, &pool_info, NULL,
        &m_descriptor_pool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool for GTAO");
    }

    m_descriptor_sets.resize(GDS_COUNT * frame_count);
    std::vector<VkDescriptorSetLayout> set_layouts(m_descriptor_sets.size(),
        m_descriptor_layout);
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = m_descriptor_sets.size();
    alloc_info.pSetLayouts = set_layouts.data();
    if (vkAllocateDescriptorSets(device, &alloc_info,
        m_descriptor_sets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor sets for GTAO");
    }

    GEVulkanDeferredFBO* dfbo =
        static_cast<GEVulkanDeferredFBO*>(m_vk->getRTTTexture());
    VkDescriptorImageInfo depth_info = {};
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth_info.imageView =
        (VkImageView)dfbo->getDepthTexture()->getTextureHandler();
    depth_info.sampler = m_vk->getSampler(GVS_NEAREST);

    VkDescriptorImageInfo normal_info = {};
    normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normal_info.imageView =
        (VkImageView)dfbo->getAttachment<GVDFT_NORMAL>()->getTextureHandler();
    normal_info.sampler = m_vk->getSampler(GVS_NEAREST);

    auto sampled_info = [&](GEVulkanTexture* texture)
    {
        VkDescriptorImageInfo info = {};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView = (VkImageView)texture->getTextureHandler();
        info.sampler = m_vk->getSampler(GVS_NEAREST);
        return info;
    };
    auto storage_info = [](GEVulkanTexture* texture)
    {
        VkDescriptorImageInfo info = {};
        info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        info.imageView = (VkImageView)texture->getTextureHandler();
        return info;
    };

    VkDescriptorImageInfo linear_sample = sampled_info(m_linear_depth);
    VkDescriptorImageInfo raw_sample = sampled_info(m_raw_ao);
    VkDescriptorImageInfo denoised_sample = sampled_info(m_denoised_ao);
    VkDescriptorImageInfo history_sample[2] =
    {
        sampled_info(m_history[0]),
        sampled_info(m_history[1])
    };
    VkDescriptorImageInfo linear_storage = storage_info(m_linear_depth);
    VkDescriptorImageInfo raw_storage = storage_info(m_raw_ao);
    VkDescriptorImageInfo denoised_storage = storage_info(m_denoised_ao);
    VkDescriptorImageInfo result_storage = storage_info(m_ao_result);
    VkDescriptorImageInfo history_storage[2] =
    {
        storage_info(m_history[0]),
        storage_info(m_history[1])
    };

    auto write_image = [](VkDescriptorSet set, uint32_t binding,
                          VkDescriptorType type, VkDescriptorImageInfo* info)
    {
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorType = type;
        write.descriptorCount = 1;
        write.pImageInfo = info;
        return write;
    };
    auto write_buffer = [](VkDescriptorSet set, VkDescriptorBufferInfo* info)
    {
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 7;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = info;
        return write;
    };

    std::vector<VkDescriptorBufferInfo> constant_infos(
        GDS_COUNT * frame_count);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(GDS_COUNT * frame_count * 8);
    for (uint32_t frame = 0; frame < frame_count; frame++)
    {
        for (uint32_t set_id = 0; set_id < GDS_COUNT; set_id++)
        {
            VkDescriptorSet set =
                m_descriptor_sets[set_id * frame_count + frame];
            VkDescriptorImageInfo* in3 = &linear_sample;
            VkDescriptorImageInfo* history = &history_sample[0];
            VkDescriptorImageInfo* out0 = &linear_storage;
            VkDescriptorImageInfo* out1 = &raw_storage;

            if (set_id == GDS_MAIN)
            {
                out0 = &raw_storage;
                out1 = &raw_storage;
            }
            else if (set_id == GDS_DENOISE)
            {
                in3 = &raw_sample;
                out0 = &denoised_storage;
                out1 = &denoised_storage;
            }
            else if (set_id == GDS_UPSAMPLE_HISTORY_0 ||
                     set_id == GDS_UPSAMPLE_HISTORY_1)
            {
                in3 = &denoised_sample;
                const unsigned read = set_id == GDS_UPSAMPLE_HISTORY_0 ? 0 : 1;
                const unsigned write = 1 - read;
                history = &history_sample[read];
                out0 = &result_storage;
                out1 = &history_storage[write];
            }

            writes.push_back(write_image(set, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depth_info));
            writes.push_back(write_image(set, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normal_info));
            writes.push_back(write_image(set, 2,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &linear_sample));
            writes.push_back(write_image(set, 3,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, in3));
            writes.push_back(write_image(set, 4,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, history));
            writes.push_back(write_image(set, 5,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, out0));
            writes.push_back(write_image(set, 6,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, out1));
            VkDescriptorBufferInfo& constants =
                constant_infos[set_id * frame_count + frame];
            constants.buffer = m_constants_buffer->getHostBuffer()[frame];
            constants.offset = 0;
            constants.range = sizeof(GTAOConstants);
            writes.push_back(write_buffer(set, &constants));
        }
    }
    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, NULL);
    m_layouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);
}   // init

// ----------------------------------------------------------------------------
void GEVulkanGTAOPass::destroy()
{
    VkDevice device = m_vk->getDevice();
    delete m_linear_depth;
    delete m_raw_ao;
    delete m_denoised_ao;
    delete m_ao_result;
    delete m_history[0];
    delete m_history[1];
    delete m_constants_buffer;
    m_linear_depth = NULL;
    m_raw_ao = NULL;
    m_denoised_ao = NULL;
    m_ao_result = NULL;
    m_history[0] = m_history[1] = NULL;
    m_constants_buffer = NULL;
    if (m_prefilter_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_prefilter_pipeline, NULL);
    if (m_main_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_main_pipeline, NULL);
    if (m_denoise_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_denoise_pipeline, NULL);
    if (m_upsample_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_upsample_pipeline, NULL);
    m_prefilter_pipeline = VK_NULL_HANDLE;
    m_main_pipeline = VK_NULL_HANDLE;
    m_denoise_pipeline = VK_NULL_HANDLE;
    m_upsample_pipeline = VK_NULL_HANDLE;
    if (m_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_pipeline_layout, NULL);
    m_pipeline_layout = VK_NULL_HANDLE;
    if (m_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, m_descriptor_pool, NULL);
    m_descriptor_pool = VK_NULL_HANDLE;
    m_descriptor_sets.clear();
    if (m_descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, m_descriptor_layout, NULL);
    m_descriptor_layout = VK_NULL_HANDLE;
    m_layouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);
}   // destroy

// ----------------------------------------------------------------------------
VkDescriptorSet GEVulkanGTAOPass::getDescriptorSet(DescriptorSetId id) const
{
    const unsigned frame = std::min<unsigned>(m_vk->getCurrentFrame(),
        m_vk->getMaxFrameInFlight() - 1);
    return m_descriptor_sets[id * m_vk->getMaxFrameInFlight() + frame];
}   // getDescriptorSet

// ----------------------------------------------------------------------------
void GEVulkanGTAOPass::transitionImage(VkCommandBuffer cmd,
                                       GEVulkanTexture* texture,
                                       unsigned layout_id,
                                       VkImageLayout new_layout,
                                       VkAccessFlags src_access,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage,
                                       VkPipelineStageFlags dst_stage)
{
    if (!texture || m_layouts[layout_id] == new_layout)
        return;
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = m_layouts[layout_id];
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = m_layouts[layout_id] == VK_IMAGE_LAYOUT_UNDEFINED ?
        0 : src_access;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        m_layouts[layout_id] == VK_IMAGE_LAYOUT_UNDEFINED ?
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : src_stage,
        dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
    m_layouts[layout_id] = new_layout;
}   // transitionImage

// ----------------------------------------------------------------------------
void GEVulkanGTAOPass::generate(VkCommandBuffer cmd)
{
    if (!m_active || m_prefilter_pipeline == VK_NULL_HANDLE)
        return;

    GTAOConstants constants = {};
    copyMatrix(constants.m_inverse_projection, m_inverse_projection);
    copyMatrix(constants.m_inverse_view, m_inverse_view);
    copyMatrix(constants.m_projection_view, m_projection_view);
    copyMatrix(constants.m_previous_projection_view,
        m_previous_projection_view);
    constants.m_viewport[0] = (float)m_viewport.UpperLeftCorner.X;
    constants.m_viewport[1] = (float)m_viewport.UpperLeftCorner.Y;
    constants.m_viewport[2] = (float)m_viewport.getWidth();
    constants.m_viewport[3] = (float)m_viewport.getHeight();
    constants.m_screen[0] = m_screen_size.Width;
    constants.m_screen[1] = m_screen_size.Height;
    constants.m_screen[2] = m_screen_size.Width > 0.0f ?
        1.0f / m_screen_size.Width : 0.0f;
    constants.m_screen[3] = m_screen_size.Height > 0.0f ?
        1.0f / m_screen_size.Height : 0.0f;
    constants.m_params0[0] = 1.25f;
    constants.m_params0[1] = 1.15f;
    constants.m_params0[2] = 0.85f;
    constants.m_params0[3] = (float)m_frame_index;
    constants.m_params1[0] = (float)m_linear_depth->getSize().Width;
    constants.m_params1[1] = (float)m_linear_depth->getSize().Height;
    constants.m_params1[2] = m_reset_history ? 1.0f : 0.0f;

    m_constants_buffer->setCurrentData(&constants, sizeof(constants), cmd);

    transitionImage(cmd, m_linear_depth, GL_LINEAR_DEPTH,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_raw_ao, GL_RAW_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_denoised_ao, GL_DENOISED_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_ao_result, GL_RESULT_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_history[0], GL_HISTORY_0,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_history[1], GL_HISTORY_1,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    VkMemoryBarrier scene_barrier = {};
    scene_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    scene_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    scene_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &scene_barrier, 0, NULL,
        0, NULL);

    const uint32_t half_w = m_linear_depth->getSize().Width;
    const uint32_t half_h = m_linear_depth->getSize().Height;
    const uint32_t full_w = m_ao_result->getSize().Width;
    const uint32_t full_h = m_ao_result->getSize().Height;

    transitionImage(cmd, m_linear_depth, GL_LINEAR_DEPTH,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_prefilter_pipeline);
    VkDescriptorSet set = getDescriptorSet(GDS_PREFILTER);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, (half_w + 7) / 8, (half_h + 7) / 8, 1);

    transitionImage(cmd, m_linear_depth, GL_LINEAR_DEPTH,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_raw_ao, GL_RAW_AO, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_main_pipeline);
    set = getDescriptorSet(GDS_MAIN);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, (half_w + 7) / 8, (half_h + 7) / 8, 1);

    transitionImage(cmd, m_raw_ao, GL_RAW_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_denoised_ao, GL_DENOISED_AO,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoise_pipeline);
    set = getDescriptorSet(GDS_DENOISE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, (half_w + 7) / 8, (half_h + 7) / 8, 1);

    transitionImage(cmd, m_denoised_ao, GL_DENOISED_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transitionImage(cmd, m_ao_result, GL_RESULT_AO,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    const unsigned history_write = 1 - m_history_index;
    transitionImage(cmd, m_history[history_write],
        history_write == 0 ? GL_HISTORY_0 : GL_HISTORY_1,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_upsample_pipeline);
    set = getDescriptorSet(m_history_index == 0 ?
        GDS_UPSAMPLE_HISTORY_0 : GDS_UPSAMPLE_HISTORY_1);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, (full_w + 7) / 8, (full_h + 7) / 8, 1);

    transitionImage(cmd, m_ao_result, GL_RESULT_AO,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    transitionImage(cmd, m_history[history_write],
        history_write == 0 ? GL_HISTORY_0 : GL_HISTORY_1,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    m_history_index = history_write;
    m_reset_history = false;
    m_frame_index++;
}   // generate

}
