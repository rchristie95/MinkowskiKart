#ifndef HEADER_GE_VULKAN_GTAO_PASS_HPP
#define HEADER_GE_VULKAN_GTAO_PASS_HPP

#include "vulkan_wrapper.h"

#include "matrix4.h"
#include "rect.h"
#include "vector3d.h"

#include <array>
#include <vector>

namespace GE
{
class GEVulkanCameraSceneNode;
class GEVulkanDriver;
class GEVulkanDynamicBuffer;
class GEVulkanTexture;

class GEVulkanGTAOPass
{
private:
    enum DescriptorSetId
    {
        GDS_PREFILTER = 0,
        GDS_MAIN,
        GDS_DENOISE,
        GDS_UPSAMPLE_HISTORY_0,
        GDS_UPSAMPLE_HISTORY_1,
        GDS_COUNT
    };

    GEVulkanDriver* m_vk;

    GEVulkanTexture* m_linear_depth;
    GEVulkanTexture* m_raw_ao;
    GEVulkanTexture* m_denoised_ao;
    GEVulkanTexture* m_ao_result;
    std::array<GEVulkanTexture*, 2> m_history;

    GEVulkanDynamicBuffer* m_constants_buffer;

    VkFormat m_ao_format;
    bool m_use_r16;
    bool m_active;
    bool m_reset_history;
    bool m_has_last_camera;
    unsigned m_history_index;
    unsigned m_frame_index;

    VkDescriptorSetLayout m_descriptor_layout;
    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_prefilter_pipeline;
    VkPipeline m_main_pipeline;
    VkPipeline m_denoise_pipeline;
    VkPipeline m_upsample_pipeline;
    VkDescriptorPool m_descriptor_pool;
    std::vector<VkDescriptorSet> m_descriptor_sets;

    irr::core::recti m_viewport;
    irr::core::dimension2df m_screen_size;
    irr::core::matrix4 m_inverse_projection;
    irr::core::matrix4 m_inverse_view;
    irr::core::matrix4 m_projection_view;
    irr::core::matrix4 m_previous_projection_view;
    irr::core::vector3df m_last_camera_position;
    irr::core::vector3df m_last_camera_direction;

    std::array<VkImageLayout, 7> m_layouts;

    // ------------------------------------------------------------------------
    void destroy();
    // ------------------------------------------------------------------------
    void init();
    // ------------------------------------------------------------------------
    VkDescriptorSet getDescriptorSet(DescriptorSetId id) const;
    // ------------------------------------------------------------------------
    void transitionImage(VkCommandBuffer cmd, GEVulkanTexture* texture,
                         unsigned layout_id, VkImageLayout new_layout,
                         VkAccessFlags src_access, VkAccessFlags dst_access,
                         VkPipelineStageFlags src_stage,
                         VkPipelineStageFlags dst_stage);
public:
    // ------------------------------------------------------------------------
    GEVulkanGTAOPass(GEVulkanDriver* vk);
    // ------------------------------------------------------------------------
    ~GEVulkanGTAOPass();
    // ------------------------------------------------------------------------
    bool prepare(GEVulkanCameraSceneNode* cam);
    // ------------------------------------------------------------------------
    void generate(VkCommandBuffer cmd);
    // ------------------------------------------------------------------------
    GEVulkanTexture* getResult() const
    {
        return m_active ? m_ao_result : NULL;
    }
};   // GEVulkanGTAOPass

}

#endif
