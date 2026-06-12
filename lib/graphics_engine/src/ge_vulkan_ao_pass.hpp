#ifndef HEADER_GE_VULKAN_AO_PASS_HPP
#define HEADER_GE_VULKAN_AO_PASS_HPP

#include "vulkan_wrapper.h"

#include "matrix4.h"
#include "rect.h"

namespace GE
{
class GEVulkanCameraSceneNode;
class GEVulkanDriver;
class GEVulkanTexture;

// Half-resolution ambient occlusion: one compute dispatch evaluates the
// Alchemy/SAO estimator from the depth buffer, a second applies a
// depth-aware blur. The blurred result is sampled by displace_color.frag
// through the draw call's data descriptor (set 1, binding 7), giving the
// same structure as the GL pipeline (AO render target + gaussian blur).
class GEVulkanAOPass
{
private:
    GEVulkanDriver* m_vk;

    // m_ao_raw = estimator output, m_ao_result = blurred (sampled later)
    GEVulkanTexture* m_ao_raw;

    GEVulkanTexture* m_ao_result;

    VkDescriptorSetLayout m_descriptor_layout;

    VkPipelineLayout m_pipeline_layout;

    VkPipeline m_ao_pipeline, m_blur_pipeline;

    VkDescriptorPool m_descriptor_pool;

    VkDescriptorSet m_ao_set, m_blur_set;

    irr::core::recti m_viewport;

    irr::core::matrix4 m_inverse_projection;

    irr::core::dimension2df m_screen_size;

    // ------------------------------------------------------------------------
    void destroy();
    // ------------------------------------------------------------------------
    void init();
public:
    // ------------------------------------------------------------------------
    GEVulkanAOPass(GEVulkanDriver* vk);
    // ------------------------------------------------------------------------
    ~GEVulkanAOPass();
    // ------------------------------------------------------------------------
    // Returns true when the AO images were (re)created and descriptors
    // referencing the result must be updated.
    bool prepare(GEVulkanCameraSceneNode* cam);
    // ------------------------------------------------------------------------
    void generate(VkCommandBuffer cmd);
    // ------------------------------------------------------------------------
    GEVulkanTexture* getResult() const                  { return m_ao_result; }
};

}

#endif
