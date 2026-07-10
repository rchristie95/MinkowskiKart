#include "ge_vulkan_draw_call.hpp"

#include "ge_culling_tool.hpp"
#include "ge_main.hpp"
#include "ge_material_manager.hpp"
#include "ge_vulkan_gtao_pass.hpp"
#include "ge_vulkan_attachment_texture.hpp"
#include "ge_vulkan_command_loader.hpp"
#include "ge_render_info.hpp"
#include "ge_spm.hpp"
#include "ge_vulkan_animated_mesh_scene_node.hpp"
#include "ge_vulkan_billboard_buffer.hpp"
#include "ge_vulkan_camera_scene_node.hpp"
#include "ge_vulkan_deferred_fbo.hpp"
#include "ge_vulkan_driver.hpp"
#include "ge_vulkan_dynamic_buffer.hpp"
#include "ge_vulkan_dynamic_spm_buffer.hpp"
#include "ge_vulkan_environment_map.hpp"
#include "ge_vulkan_features.hpp"
#include "ge_vulkan_hiz_depth.hpp"
#include "ge_vulkan_light_handler.hpp"
#include "ge_vulkan_mesh_cache.hpp"
#include "ge_vulkan_mesh_scene_node.hpp"
#include "ge_vulkan_shader_manager.hpp"
#include "ge_vulkan_skybox_renderer.hpp"
#include "ge_vulkan_texture_descriptor.hpp"

#include "mini_glm.hpp"
#include "IBillboardSceneNode.h"
#include "ILightSceneNode.h"
#include "IParticleSystemSceneNode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../source/Irrlicht/os.h"
#include "quaternion.h"
#define SKINNING_PIPELINE "_skinning"

namespace GE
{
// ============================================================================
static void destroyPipeline(VkPipeline* p)
{
    vkDestroyPipeline(static_cast<GEVulkanDriver*>(getDriver())->getDevice(),
        *p, NULL);
    delete p;
}   // destroyPipeline

// ============================================================================
PipelineSettings::PipelineSettings()
{
    m_drawing_priority = (char)0;
    m_custom_pl = VK_NULL_HANDLE;
    m_depth_op = VK_COMPARE_OP_LESS;
    m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_pipeline_type = GVPT_SOLID;
    GEMaterial default_material;
    loadMaterial(default_material);
}   // PipelineSettings::PipelineSettings

// ============================================================================
void PipelineSettings::loadMaterial(const GEMaterial& m)
{
    m_material = std::make_shared<const GEMaterial>(m);
}   // PipelineSettings::loadMaterial

// ============================================================================
void ObjectData::init(irr::scene::ISceneNode* node, int material_id,
                      int skinning_offset, int irrlicht_material_id)
{
    using namespace MiniGLM;
    const irr::core::matrix4& model_mat = node->getAbsoluteTransformation();
    irr::core::quaternion rotation(0.0f, 0.0f, 0.0f, 1.0f);
    irr::core::vector3df scale = model_mat.getScale();
    if (scale.X != 0.0f && scale.Y != 0.0f && scale.Z != 0.0f)
    {
        irr::core::matrix4 local_mat = model_mat;
        local_mat[0] = local_mat[0] / scale.X / local_mat[15];
        local_mat[1] = local_mat[1] / scale.X / local_mat[15];
        local_mat[2] = local_mat[2] / scale.X / local_mat[15];
        local_mat[4] = local_mat[4] / scale.Y / local_mat[15];
        local_mat[5] = local_mat[5] / scale.Y / local_mat[15];
        local_mat[6] = local_mat[6] / scale.Y / local_mat[15];
        local_mat[8] = local_mat[8] / scale.Z / local_mat[15];
        local_mat[9] = local_mat[9] / scale.Z / local_mat[15];
        local_mat[10] = local_mat[10] / scale.Z / local_mat[15];
        rotation = getQuaternion(local_mat);
        // Conjugated quaternion in glsl
        rotation.W = -rotation.W;
    }
    memcpy(&m_translation_x, &node->getAbsoluteTransformation()[12],
        sizeof(float) * 3);
    memcpy(m_rotation, &rotation, sizeof(irr::core::quaternion));
    memcpy(&m_scale_x, &scale, sizeof(irr::core::vector3df));
    m_skinning_offset = skinning_offset;
    m_material_id = material_id;
    const irr::core::matrix4& texture_matrix =
        node->getMaterial(irrlicht_material_id).getTextureMatrix(0);
    m_texture_trans[0] = texture_matrix[8];
    m_texture_trans[1] = texture_matrix[9];
    if (GENodeVelocityFunction velocity_function = getNodeVelocityFunction())
    {
        velocity_function(node,
            &node->getMaterial(irrlicht_material_id), m_velocity);
    }
    else
        m_velocity[0] = m_velocity[1] = m_velocity[2] = m_velocity[3] = 0.0f;
    if (GENodeGlowColorFunction glow_function = getNodeGlowColorFunction())
        glow_function(node, m_glow_color);
    else
    {
        m_glow_color[0] = m_glow_color[1] = m_glow_color[2] =
            m_glow_color[3] = 0.0f;
    }
    auto& ri = node->getMaterial(irrlicht_material_id).getRenderInfo();
    if (ri && ri->getHue() > 0.0f)
        m_hue_change = ri->getHue();
    else
        m_hue_change = 0.0f;
    if (ri)
    {
        if (getGEConfig()->m_pbr)
        {
            m_custom_vertex_color =
                srgb255ToLinearFromSColor(ri->getVertexColor()).color;
        }
        else
        {
            m_custom_vertex_color = ri->getVertexColor();
        }
    }
    else
    {
        m_custom_vertex_color = irr::video::SColor((uint32_t)-1);
    }
}   // init

// ============================================================================
void ObjectData::init(irr::scene::IBillboardSceneNode* node, int material_id,
                      const btQuaternion& rotation)
{
    memcpy(&m_translation_x, &node->getAbsoluteTransformation()[12],
        sizeof(float) * 3);
    memcpy(m_rotation, &rotation[0], sizeof(btQuaternion));
    irr::core::vector2df billboard_size = node->getSize();
    m_scale_x = billboard_size.X / 2.0f;
    m_scale_y = billboard_size.Y / 2.0f;
    m_scale_z = 0.0f;
    m_skinning_offset = 0;
    m_material_id = material_id;
    m_texture_trans[0] = 0.0f;
    m_texture_trans[1] = 0.0f;
    m_hue_change = 0.0f;
    m_velocity[0] = m_velocity[1] = m_velocity[2] = m_velocity[3] = 0.0f;
    m_glow_color[0] = m_glow_color[1] = m_glow_color[2] =
        m_glow_color[3] = 0.0f;
    // Only support average of them at the moment
    irr::video::SColor top, bottom, output;
    node->getColor(top, bottom);
    output.setAlpha((top.getAlpha() + bottom.getAlpha()) / 2);
    output.setRed((top.getRed() + bottom.getRed()) / 2);
    output.setGreen((top.getGreen() + bottom.getGreen()) / 2);
    output.setBlue((top.getBlue() + bottom.getBlue()) / 2);
    if (getGEConfig()->m_pbr)
        output = srgb255ToLinearFromSColor(output).color;
    m_custom_vertex_color = output;
}   // init

// ============================================================================
std::vector<float> g_flips_data;
// ============================================================================
void ObjectData::init(const irr::scene::SParticle& particle, int material_id,
                      const btQuaternion& rotation,
                      const irr::core::vector3df& view_position, bool flips,
                      bool sky_particle, bool backface_culling)
{
    memcpy(&m_translation_x, &particle.pos, sizeof(float) * 3);
    float scale_x = particle.size.Width / 2.0f;
    if (flips)
    {
        // Following stk_particle.cpp
        const unsigned particle_index = particle.startTime;
        const float lifetime = particle.startSize.Width;
        const float pi = 3.14159265358979323846f;
        while (particle_index + 1 > g_flips_data.size())
        {
            // Maximum 3 rotation around axis (0, 1, 0) during lifetime
            g_flips_data.push_back(pi * 2.0f * 3.0f * os::Randomizer::frand() *
                (g_flips_data.size() % 2 == 0 ? 1.0f : -1.0f));
        }
        float angle = fmodf(lifetime * g_flips_data[particle_index],
            pi * 2.0f);
        btQuaternion rotated(btVector3(0.0f, 1.0f, 0.0f), angle);
        rotated = btQuaternion(rotation[0], rotation[1], rotation[2],
            -rotation[3]) * rotated;
        rotated.normalize();
        // Conjugated quaternion in glsl
        rotated[3] = -rotated[3];
        memcpy(m_rotation, &rotated[0], sizeof(btQuaternion));
        if (backface_culling)
        {
            irr::core::quaternion q(rotated[0], rotated[1], rotated[2],
                -rotated[3]);
            irr::core::matrix4 m;
            q.getMatrix(m, particle.pos);
            irr::core::vector3df tri[3] =
            {
                irr::core::vector3df( 1.0f, -1.0f, 0.0f),
                irr::core::vector3df( 1.0f,  1.0f, 0.0f),
                irr::core::vector3df(-1.0f,  1.0f, 0.0f)
            };
            m.transformVect(tri[0]);
            m.transformVect(tri[1]);
            m.transformVect(tri[2]);
            irr::core::vector3df normal = (tri[1] - tri[0])
                .crossProduct(tri[2] - tri[0]);
            float dot_product = (tri[0] - view_position).dotProduct(normal);
            if (dot_product < 0.0f)
                scale_x = -scale_x;
        }
    }
    else if (sky_particle)
    {
        irr::core::vector3df diff = particle.pos - view_position;
        float angle = atan2f(diff.X, diff.Z);
        btQuaternion rotated(btVector3(0.0f, 1.0f, 0.0f), angle);
        rotated.normalize();
        // Conjugated quaternion in glsl
        rotated[3] = -rotated[3];
        memcpy(m_rotation, &rotated[0], sizeof(btQuaternion));
    }
    else
        memcpy(m_rotation, &rotation[0], sizeof(btQuaternion));
    m_scale_x = scale_x;
    m_scale_y = particle.size.Height / 2.0f;
    m_scale_z = 0.0f;
    m_skinning_offset = 0;
    m_material_id = material_id;
    m_texture_trans[0] = 0.0f;
    m_texture_trans[1] = 0.0f;
    m_hue_change = 0.0f;
    m_velocity[0] = m_velocity[1] = m_velocity[2] = m_velocity[3] = 0.0f;
    m_glow_color[0] = m_glow_color[1] = m_glow_color[2] =
        m_glow_color[3] = 0.0f;
    if (getGEConfig()->m_pbr)
        m_custom_vertex_color = srgb255ToLinearFromSColor(particle.color).color;
    else
        m_custom_vertex_color = particle.color;
}   // init

// ----------------------------------------------------------------------------
GEVulkanDrawCall::GEVulkanDrawCall()
                : m_limits(getVKDriver()->getPhysicalDeviceProperties().limits)
{
    m_culling_tool = new GECullingTool;
    m_light_handler = NULL;
    m_dynamic_data = NULL;
    m_sbo_data = NULL;
    m_object_data_padded_size = 0;
    m_skinning_data_padded_size = 0;
    m_materials_padded_size = 0;
    m_dynamic_spm_padded_size = 0;
    m_data_descriptor_generation = 1;
    m_data_layout = VK_NULL_HANDLE;
    m_descriptor_pool = VK_NULL_HANDLE;
    m_pipeline_layout = VK_NULL_HANDLE;
    m_skybox_layout = VK_NULL_HANDLE;
    m_skybox_renderer = NULL;
    m_shadow_map = NULL;
    m_shadow_render_pass = VK_NULL_HANDLE;
    m_shadow_framebuffer = VK_NULL_HANDLE;
    m_shadow_cascade = 0;
    GEVulkanDriver* vk = static_cast<GEVulkanDriver*>(getDriver());
    m_texture_descriptor = vk->getMeshTextureDescriptor();
    GEVulkanDeferredFBO* dfbo =
        dynamic_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
    if (dfbo && dfbo->getAttachment<GVDFT_DISPLACE_COLOR>() &&
        getGEConfig()->m_screen_space_reflection_type >= GSSRT_HIZ)
        m_hiz_depth = new GEVulkanHiZDepth(vk);
    else
        m_hiz_depth = NULL;
    if (dfbo)
        m_gtao_pass = new GEVulkanGTAOPass(vk);
    else
        m_gtao_pass = NULL;
}   // GEVulkanDrawCall

// ----------------------------------------------------------------------------
GEVulkanDrawCall::~GEVulkanDrawCall()
{
    // Descriptor pools, pipeline layouts and pipelines below are not yet part
    // of the driver's deferred-retirement queue. Keep their destruction safe;
    // callers that already drained the device set m_disable_wait_idle to avoid
    // repeating the wait for every cached draw call.
    GEVulkanDriver* vk = getVKDriver();
    if (vk)
        vk->waitIdle();
    delete m_culling_tool;
    delete m_light_handler;
    delete m_dynamic_data;
    delete m_sbo_data;
    for (auto& p : m_billboard_buffers)
       p.second->drop();
    if (m_data_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(vk->getDevice(), m_data_layout, NULL);
        vkDestroyDescriptorPool(vk->getDevice(), m_descriptor_pool, NULL);
        m_graphics_pipelines.clear();
        vkDestroyPipelineLayout(vk->getDevice(), m_pipeline_layout, NULL);
        vkDestroyPipelineLayout(vk->getDevice(), m_skybox_layout, NULL);
        for (VkPipelineLayout layout : m_deferred_layouts)
        {
            if (layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(vk->getDevice(), layout, NULL);
        }
        if (m_shadow_framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(vk->getDevice(), m_shadow_framebuffer, NULL);
        if (m_shadow_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(vk->getDevice(), m_shadow_render_pass, NULL);
        delete m_shadow_map;
    }
    delete m_hiz_depth;
    delete m_gtao_pass;
}   // ~GEVulkanDrawCall

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::addNode(irr::scene::ISceneNode* node)
{
    irr::scene::IMesh* mesh;
    GEVulkanAnimatedMeshSceneNode* anode = NULL;
    if (node->getType() == irr::scene::ESNT_ANIMATED_MESH)
    {
        anode = static_cast<GEVulkanAnimatedMeshSceneNode*>(node);
        mesh = anode->getMesh();
    }
    else if (node->getType() == irr::scene::ESNT_MESH)
    {
        mesh = static_cast<irr::scene::IMeshSceneNode*>(node)->getMesh();
        for (unsigned i = 0; i < mesh->getMeshBufferCount(); i++)
        {
            irr::scene::IMeshBuffer* b = mesh->getMeshBuffer(i);
            if (b->getVertexType() != irr::video::EVT_SKINNED_MESH)
                return;
        }
    }
    else
        return;

    bool added_skinning = false;
    for (unsigned i = 0; i < mesh->getMeshBufferCount(); i++)
    {
        GESPMBuffer* buffer = static_cast<GESPMBuffer*>(
            mesh->getMeshBuffer(i));
        if (m_culling_tool->isCulled(buffer, node, i))
            continue;
        const bool dynamic_spm =
            buffer->getHardwareMappingHint_Vertex() == irr::scene::EHM_STREAM ||
            buffer->getHardwareMappingHint_Index() == irr::scene::EHM_STREAM;
        const std::string shader = getShader(node, i,
            /*allow_tessellation*/!dynamic_spm);
        if (dynamic_spm)
        {
            GEVulkanDynamicSPMBuffer* dbuffer = static_cast<
                GEVulkanDynamicSPMBuffer*>(buffer);
            m_dynamic_spm_buffers[getDynamicBufferKey(shader)]
                .emplace_back(dbuffer, node);
            continue;
        }
        const irr::video::SMaterial& m = node->getMaterial(i);
        TexturesList t = getTexturesList(m);
        std::pair<GESPMBuffer*, TexturesList> k = std::make_pair(buffer, t);
        m_visible_nodes[k][getShader(m)].emplace_back(node, i);
        m_mb_map[k] = mesh;
        if (anode && !added_skinning &&
            !anode->getSkinningMatrices().empty() &&
            m_skinning_nodes.find(anode) == m_skinning_nodes.end())
        {
            added_skinning = true;
            m_skinning_nodes.insert(anode);
        }
    }
}   // addNode

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::addBillboardNode(irr::scene::ISceneNode* node,
                                        irr::scene::ESCENE_NODE_TYPE node_type)
{
    irr::core::aabbox3df bb = node->getTransformedBoundingBox();
    if (m_culling_tool->isCulled(bb))
        return;
    irr::video::SMaterial m = node->getMaterial(0);
    TexturesList textures = {};
    if (!GEVulkanFeatures::supportsDifferentTexturePerDraw() ||
        !GEVulkanFeatures::supportsBindMeshTexturesAtOnce())
        textures = getTexturesList(m);
    if (m_billboard_buffers.find(textures) == m_billboard_buffers.end())
        m_billboard_buffers[textures] = new GEVulkanBillboardBuffer(m);
    GESPMBuffer* buffer = m_billboard_buffers.at(textures);
    const std::string& shader = getShader(node, 0);
    std::pair<GESPMBuffer*, TexturesList> k = std::make_pair(buffer, textures);
    m_visible_nodes[k][shader].emplace_back(node,
        node_type == irr::scene::ESNT_BILLBOARD ? BILLBOARD_NODE :
        PARTICLE_NODE);
}   // addBillboardNode

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::generate(GEVulkanDriver* vk)
{
    if (!m_visible_nodes.empty() && m_data_layout == VK_NULL_HANDLE)
        createVulkanData();

    if (m_light_handler)
         m_light_handler->generate(m_view_position, m_skybox_renderer);

    using Nodes = std::pair<std::pair<GESPMBuffer*, TexturesList>, std::unordered_map<
        std::string, std::vector<std::pair<irr::scene::ISceneNode*, int
        > > > >;
    std::vector<Nodes> visible_nodes;

    for (auto& p : m_visible_nodes)
        visible_nodes.emplace_back(p.first, std::move(p.second));
    std::unordered_map<GESPMBuffer*, float> nodes_area;
    for (auto& p : visible_nodes)
    {
        if (p.second.empty())
        {
            nodes_area[p.first.first] = std::numeric_limits<float>::max();
            continue;
        }
        for (auto& q : p.second)
        {
            if (q.second.empty())
            {
                nodes_area[p.first.first] = std::numeric_limits<float>::max();
                continue;
            }
            irr::core::aabbox3df bb = p.first.first->getBoundingBox();
            q.second[0].first->getAbsoluteTransformation().transformBoxEx(bb);
            nodes_area[p.first.first] = bb.getArea() * (float)q.second.size();
            break;
        }
    }
    std::stable_sort(visible_nodes.begin(), visible_nodes.end(),
        [&nodes_area](const Nodes& a, const Nodes& b)
        {
            return nodes_area.at(a.first.first) < nodes_area.at(b.first.first);
        });

    size_t min_size = 0;
start:
    m_cmds.clear();
    m_dyspmb_materials.clear();
    m_materials_data.clear();

    if (m_sbo_data->resizeIfNeeded(min_size))
        invalidateDataDescriptorSets();
    int current_buffer_idx = vk->getCurrentBufferIdx();
    uint8_t* mapped_addr = (uint8_t*)m_sbo_data->getMappedAddr()
        [current_buffer_idx];

    std::unordered_map<irr::scene::ISceneNode*, int> skinning_offets;
    int added_joint = 1;
    size_t skinning_data_padded_size = sizeof(irr::core::matrix4);

    static irr::core::matrix4 identity = irr::core::matrix4();
    memcpy(mapped_addr, identity.pointer(), sizeof(irr::core::matrix4));
    size_t written_size = sizeof(irr::core::matrix4);
    mapped_addr += sizeof(irr::core::matrix4);

    for (GEVulkanAnimatedMeshSceneNode* node : m_skinning_nodes)
    {
        int bone_count = node->getSPM()->getJointCount();
        size_t bone_size = sizeof(irr::core::matrix4) * bone_count;
        if (written_size + bone_size > m_sbo_data->getSize())
        {
            min_size = (written_size + bone_size) * 2;
            goto start;
        }
        memcpy(mapped_addr, node->getSkinningMatrices().data(), bone_size);
        written_size += bone_size;
        mapped_addr += bone_size;
        skinning_offets[node] = added_joint;
        added_joint += bone_count;
        skinning_data_padded_size += bone_size;
    }

    size_t sbo_alignment = m_limits.minStorageBufferOffsetAlignment;
    if (skinning_data_padded_size > m_skinning_data_padded_size)
    {
        invalidateDataDescriptorSets();
        size_t skinning_padding = getPadding(skinning_data_padded_size,
            sbo_alignment);
        if (skinning_padding > 0)
        {
            if (written_size + skinning_padding > m_sbo_data->getSize())
            {
                min_size = (written_size + skinning_padding) * 2;
                goto start;
            }
            skinning_data_padded_size += skinning_padding;
            written_size += skinning_padding;
            mapped_addr += skinning_padding;
        }
        m_skinning_data_padded_size = skinning_data_padded_size;
    }
    else
    {
        size_t extra = m_skinning_data_padded_size - skinning_data_padded_size;
        if (written_size + extra > m_sbo_data->getSize())
        {
            min_size = (written_size + extra) * 2;
            goto start;
        }
        skinning_data_padded_size = m_skinning_data_padded_size;
        written_size += extra;
        mapped_addr += extra;
    }

    size_t dynamic_spm_offset = 0;
    for (auto& p : m_dynamic_spm_buffers)
    {
        for (auto& q : p.second)
        {
            const size_t dynamic_spm_size = sizeof(ObjectData) +
                getPadding(sizeof(ObjectData), sbo_alignment);
            if (written_size + dynamic_spm_size > m_sbo_data->getSize())
            {
                min_size = (written_size + dynamic_spm_size) * 2;
                goto start;
            }
            irr::scene::ISceneNode* node = q.second;
            const irr::video::SMaterial& m = node->getMaterial(0);
            TexturesList textures = getTexturesList(m);
            const irr::video::ITexture** list = &textures[0];
            int material_id = m_texture_descriptor->getTextureID(list,
                getShader(m));
            ObjectData* data = (ObjectData*)mapped_addr;
            data->init(node, material_id, -1, 0);
            m_dyspmb_materials[q.first] = std::make_pair(material_id,
                dynamic_spm_offset);
            written_size += dynamic_spm_size;
            mapped_addr += dynamic_spm_size;
            dynamic_spm_offset += dynamic_spm_size;
        }
    }
    m_dynamic_spm_padded_size = written_size - skinning_data_padded_size;

    const bool use_base_vertex = GEVulkanFeatures::supportsBaseVertexRendering();
    const bool bind_mesh_textures =
        GEVulkanFeatures::supportsBindMeshTexturesAtOnce();
    unsigned accumulated_instance = 0;

    struct InstanceKey
    {
        std::vector<irr::scene::ISceneNode*> m_nodes;
        unsigned m_instance_count;
        unsigned m_first_instance;
        bool m_hue_change;
    };
    std::unordered_map<irr::scene::IMesh*, std::vector<InstanceKey> >
        instance_keys;
    size_t instance_offset = 0;
    const uint8_t* instance_start_ptr = mapped_addr;
    std::unordered_map<uint32_t, uint32_t> offset_map;
    for (auto& p : visible_nodes)
    {
        GESPMBuffer* mb = p.first.first;
        TexturesList& textures = p.first.second;
        const irr::video::ITexture** list = &textures[0];
        const bool skinning = mb->hasSkinning();
        for (auto& q : p.second)
        {
            unsigned visible_count = q.second.size();
            if (visible_count == 0)
                continue;
            std::string cur_shader = q.first;
            int material_id = m_texture_descriptor->getTextureID(list,
                cur_shader);
            if (skinning)
                cur_shader += SKINNING_PIPELINE;
            if (m_graphics_pipelines.find(cur_shader) ==
                m_graphics_pipelines.end())
                continue;
            // Mark batches containing glowing nodes for the GVPT_GLOW pass.
            bool group_glow = false;
            if (GENodeGlowColorFunction gf = getNodeGlowColorFunction())
            {
                for (auto& r : q.second)
                {
                    if (r.second == BILLBOARD_NODE || r.second == PARTICLE_NODE)
                        break;
                    float glow[4];
                    gf(r.first, glow);
                    if (glow[3] > 0.0f)
                    {
                        group_glow = true;
                        break;
                    }
                }
            }
            InstanceKey key;
            key.m_nodes.reserve(q.second.size());
            key.m_instance_count = visible_count;
            key.m_first_instance = accumulated_instance;
            key.m_hue_change = false;
            bool skip_instance_key = false;
            for (auto& r : q.second)
            {
                if (r.second == BILLBOARD_NODE || r.second == PARTICLE_NODE)
                {
                    skip_instance_key = true;
                    break;
                }
                irr::scene::ISceneNode* node = r.first;
                const irr::core::matrix4& texture_matrix =
                    node->getMaterial(r.second).getTextureMatrix(0);
                if (texture_matrix[8] != 0.0f || texture_matrix[9] != 0.0f)
                {
                    skip_instance_key = true;
                    break;
                }
                auto& ri = node->getMaterial(r.second).getRenderInfo();
                if (ri && ri->getHue() > 0.0f)
                {
                    key.m_hue_change = true;
                    break;
                }
                key.m_nodes.push_back(node);
            }
            irr::scene::IMesh* m = m_mb_map[std::make_pair(mb, textures)];
            auto& cur_key = instance_keys[m];
            auto it = cur_key.end();
            if (!skip_instance_key)
            {
                it = std::find_if(cur_key.begin(), cur_key.end(),
                    [key](const InstanceKey& k)
                    {
                        return k.m_nodes == key.m_nodes &&
                            k.m_instance_count == key.m_instance_count &&
                            k.m_hue_change == key.m_hue_change;
                    });
            }
            const PipelineSettings& settings =
                m_graphics_pipelines[cur_shader].m_settings;
            for (auto& r : q.second)
            {
                irr::scene::ISceneNode* node = r.first;
                if (r.second == BILLBOARD_NODE || r.second == PARTICLE_NODE)
                {
                    if (GEVulkanFeatures::supportsDifferentTexturePerDraw())
                    {
                        const irr::video::SMaterial& m = node->getMaterial(0);
                        TexturesList textures = getTexturesList(m);
                        const irr::video::ITexture** list = &textures[0];
                        material_id = m_texture_descriptor->getTextureID(list,
                            getShader(m));
                    }
                    if (r.second == BILLBOARD_NODE)
                    {
                        if (written_size + sizeof(ObjectData) >
                            m_sbo_data->getSize())
                        {
                            min_size = (written_size + sizeof(ObjectData)) * 2;
                            goto start;
                        }
                        ObjectData* obj = (ObjectData*)mapped_addr;
                        obj->init(
                            static_cast<irr::scene::IBillboardSceneNode*>(
                            node), material_id, m_billboard_rotation);
                        written_size += sizeof(ObjectData);
                        mapped_addr += sizeof(ObjectData);
                    }
                    else
                    {
                        irr::scene::IParticleSystemSceneNode* pn =
                            static_cast<irr::scene::IParticleSystemSceneNode*>(
                            node);
                        const core::array<SParticle>& particles =
                            pn->getParticles();
                        unsigned ps = particles.size();
                        if (ps == 0)
                        {
                            visible_count--;
                            continue;
                        }
                        visible_count += ps - 1;
                        if (written_size + sizeof(ObjectData) * ps >
                            m_sbo_data->getSize())
                        {
                            min_size =
                                (written_size + sizeof(ObjectData) * ps) * 2;
                            goto start;
                        }
                        ObjectData* obj = (ObjectData*)mapped_addr;
                        bool flips = pn->getFlips();
                        bool sky_particle = pn->isSkyParticle();
                        for (unsigned i = 0; i < ps; i++)
                        {
                            obj[i].init(particles[i], material_id,
                                m_billboard_rotation, m_view_position, flips,
                                sky_particle,
                                settings.m_material->m_backface_culling);
                            written_size += sizeof(ObjectData);
                            mapped_addr += sizeof(ObjectData);
                        }
                    }
                }
                else if (skip_instance_key || it == cur_key.end())
                {
                    int skinning_offset = -1000;
                    auto it = skinning_offets.find(node);
                    if (it != skinning_offets.end())
                        skinning_offset = it->second;
                    if (written_size + sizeof(ObjectData) >
                        m_sbo_data->getSize())
                    {
                        min_size = (written_size + sizeof(ObjectData)) * 2;
                        goto start;
                    }
                    ObjectData* obj = (ObjectData*)mapped_addr;
                    obj->init(node, bind_mesh_textures ? -1 : material_id,
                        skinning_offset, r.second);
                    written_size += sizeof(ObjectData);
                    mapped_addr += sizeof(ObjectData);
                }
            }
            VkDrawIndexedIndirectCommand cmd;
            cmd.indexCount = mb->getIndexCount();
            cmd.instanceCount = visible_count;
            cmd.firstIndex = use_base_vertex ? mb->getIBOOffset() : 0;
            cmd.vertexOffset = use_base_vertex ? mb->getVBOOffset() : 0;
            if (skip_instance_key || it == cur_key.end())
            {
                cmd.firstInstance = accumulated_instance;
                if (!use_base_vertex)
                {
                    offset_map[accumulated_instance] = instance_offset;
                    size_t instance_padding = getPadding(written_size,
                        sbo_alignment);
                    if (instance_padding > 0)
                    {
                        if (written_size + instance_padding >
                            m_sbo_data->getSize())
                        {
                            min_size = (written_size + instance_padding) * 2;
                            goto start;
                        }
                        written_size += instance_padding;
                        mapped_addr += instance_padding;
                    }
                    instance_offset = mapped_addr - instance_start_ptr;
                }
                accumulated_instance += visible_count;
            }
            else
                cmd.firstInstance = it->m_first_instance;
            std::string sorting_key =
                std::string(1, settings.m_drawing_priority) + cur_shader;
            m_cmds.push_back({ cmd, cur_shader, sorting_key, mb, material_id,
                offset_map[cmd.firstInstance], group_glow });
            if (!skip_instance_key && it == cur_key.end())
                 cur_key.push_back(key);
        }
    }
    if (!bind_mesh_textures)
    {
        std::stable_sort(m_cmds.begin(), m_cmds.end(),
            [this](const DrawCallData& a, const DrawCallData& b)
            {
                return a.m_material_id < b.m_material_id;
            });
    }

    std::stable_sort(m_cmds.begin(), m_cmds.end(),
        [](const DrawCallData& a, const DrawCallData& b)
        {
            return a.m_sorting_key < b.m_sorting_key;
        });

    size_t object_data_padded_size = written_size - skinning_data_padded_size;
    if (object_data_padded_size > m_object_data_padded_size)
    {
        invalidateDataDescriptorSets();
        size_t object_padding = getPadding(written_size, sbo_alignment);
        if (object_padding > 0)
        {
            if (written_size + object_padding > m_sbo_data->getSize())
            {
                min_size = (written_size + object_padding) * 2;
                goto start;
            }
            object_data_padded_size += object_padding;
            written_size += object_padding;
            mapped_addr += object_padding;
        }
        m_object_data_padded_size = object_data_padded_size;
    }
    else
    {
        size_t extra = m_object_data_padded_size - object_data_padded_size;
        if (written_size + extra > m_sbo_data->getSize())
        {
            min_size = (written_size + extra) * 2;
            goto start;
        }
        object_data_padded_size = m_object_data_padded_size;
        written_size += extra;
        mapped_addr += extra;
    }

    size_t materials_padded_size = 0;
    if (bind_mesh_textures && !m_cmds.empty())
    {
        std::string cur_shader = m_cmds[0].m_shader;
        for (unsigned i = 0; i < m_cmds.size(); i++)
        {
            auto& cmd = m_cmds[i];
            auto& material = m_materials_data[cur_shader];
            if (cmd.m_shader != cur_shader)
            {
                size_t material_size = material.second.size() * sizeof(int);
                if (written_size + material_size > m_sbo_data->getSize())
                {
                    min_size = (written_size + material_size) * 2;
                    goto start;
                }
                memcpy(mapped_addr, material.second.data(), material_size);
                written_size += material_size;
                mapped_addr += material_size;
                size_t cur_padding = getPadding(written_size, sbo_alignment);
                if (cur_padding > 0)
                {
                    if (written_size + cur_padding > m_sbo_data->getSize())
                    {
                        min_size = (written_size + cur_padding) * 2;
                        goto start;
                    }
                    written_size += cur_padding;
                    mapped_addr += cur_padding;
                    material_size += cur_padding;
                }
                material.first = materials_padded_size;
                materials_padded_size += material_size;
                cur_shader = cmd.m_shader;
            }
            m_materials_data[cmd.m_shader].second
                .push_back(cmd.m_material_id);
        }

        auto& material = m_materials_data[m_cmds.back().m_shader];
        size_t material_size = material.second.size() * sizeof(int);
        if (written_size + material_size > m_sbo_data->getSize())
        {
            min_size = (written_size + material_size) * 2;
            goto start;
        }
        memcpy(mapped_addr, material.second.data(), material_size);
        written_size += material_size;
        mapped_addr += material_size;
        size_t cur_padding = getPadding(written_size, sbo_alignment);
        if (cur_padding > 0)
        {
            if (written_size + cur_padding > m_sbo_data->getSize())
            {
                min_size = (written_size + cur_padding) * 2;
                goto start;
            }
            written_size += cur_padding;
            mapped_addr += cur_padding;
            material_size += cur_padding;
        }
        material.first = materials_padded_size;
        materials_padded_size += material_size;
        if (materials_padded_size > m_materials_padded_size)
        {
            invalidateDataDescriptorSets();
            m_materials_padded_size = materials_padded_size;
        }

        // Make sure dynamic offsets won't become invalid
        if (skinning_data_padded_size + (object_data_padded_size * 2) +
            (materials_padded_size * 2) > m_sbo_data->getSize())
        {
            min_size = skinning_data_padded_size +
                (object_data_padded_size * 2) + (materials_padded_size * 2);
            goto start;
        }
    }
    else
    {
        // For hasShaderForRendering
        for (unsigned i = 0; i < m_cmds.size(); i++)
            m_materials_data[m_cmds[i].m_shader] = {};
        // Make sure dynamic offset of objects won't become invalid
        if (skinning_data_padded_size + (object_data_padded_size * 2) >
            m_sbo_data->getSize())
        {
            min_size = skinning_data_padded_size +
                (object_data_padded_size * 2);
            goto start;
        }
    }
}   // generate

// ----------------------------------------------------------------------------
std::string GEVulkanDrawCall::getShader(irr::scene::ISceneNode* node,
                                        int material_id,
                                        bool allow_tessellation)
{
    irr::video::SMaterial& m = node->getMaterial(material_id);
    return getShader(m, allow_tessellation);
}   // getShader

// ----------------------------------------------------------------------------
std::string GEVulkanDrawCall::getShader(const irr::video::SMaterial& m,
                                        bool allow_tessellation)
{
    std::string shader = GEMaterialManager::getShader(m.MaterialType);
    auto material = GEMaterialManager::getMaterial(shader);
    if ((!getGEConfig()->m_pbr && !material->m_nonpbr_fallback.empty()) ||
        ((m_deferred_layouts.empty() ||
        m_deferred_layouts[GVDFP_DISPLACE_COLOR] == VK_NULL_HANDLE) &&
        shader == "displace"))
    {
        shader = material->m_nonpbr_fallback;
        material = GEMaterialManager::getMaterial(shader);
    }
    auto& ri = m.getRenderInfo();
    // Use real transparent shader first
    if (!material->isTransparent() && ri && ri->isTransparent())
        return "ghost";
    // Honour the per-mesh-buffer backface culling flag (set from the track's
    // materials.xml backface-culling="N" under STK) like the GL pipeline.
    if (!m.BackfaceCulling && material && material->m_backface_culling)
    {
        std::string two_sided = shader + "_2sided";
        if (GEMaterialManager::getMaterial(two_sided))
            shader = two_sided;
    }
    // With relativistic warping active, route static geometry through the
    // adaptively tessellated variant so large triangles (ocean planes etc.)
    // subdivide and warp smoothly per-vertex. Skinned draws of the same
    // variant use a pipeline built without the tessellation stages.
    if (allow_tessellation &&
        getGEConfig()->m_adaptive_tessellation &&
        getVKDriver()->getPhysicalDeviceFeatures().tessellationShader ==
        VK_TRUE)
    {
        std::string tess = shader + "_tess";
        if (GEMaterialManager::getMaterial(tess))
            return tess;
    }
    return shader;
}   // getShader

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::prepare(GEVulkanCameraSceneNode* cam)
{
    reset();
    if (getGEConfig()->m_pbr && m_light_handler == NULL)
        m_light_handler = new GEVulkanLightHandler(getVKDriver());
    if (m_light_handler)
        m_light_handler->prepare();
    m_culling_tool->init(cam);
    m_view_position = cam->getAbsolutePosition();
    m_billboard_rotation = MiniGLM::getBulletQuaternion(cam->getViewMatrix());
    if (m_hiz_depth)
        m_hiz_depth->prepare(cam);
    if (m_gtao_pass)
    {
        if (m_gtao_pass->prepare(cam))
            invalidateDataDescriptorSets();
    }
}   // prepare

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::createAllPipelines(GEVulkanDriver* vk)
{
    PipelineSettings settings;
    GEMaterial def_mat = *GEMaterialManager::getMaterial("solid");
    settings.m_vertex_description = getDefaultVertexDescription();
    std::unordered_map<std::string, std::shared_ptr<VkPipeline> > dp_cache;
    int drawing_order = 1;
    // Skip tessellation materials on devices without the feature: creating
    // pipelines with tessellation stages there is undefined behaviour.
    const bool supports_tess =
        vk->getPhysicalDeviceFeatures().tessellationShader == VK_TRUE;
    auto skip_material = [supports_tess](
        const std::shared_ptr<const GEMaterial>& m)
    {
        return !supports_tess &&
            !m->m_tesc_shader.empty() && !m->m_tese_shader.empty();
    };

    for (auto& p : GEMaterialManager::g_materials)
    {
        if (p.second->isTransparent() || skip_material(p.second))
            continue;
        if (!getGEConfig()->m_pbr && !p.second->m_nonpbr_fallback.empty())
            continue;
        settings.m_drawing_priority = (char)drawing_order;
        drawing_order = drawing_order + 1;
        settings.m_shader_name = p.first;
        settings.m_material = p.second;
        createPipeline(vk, settings, dp_cache);
    }

    def_mat.m_fragment_shader = "depth_only.frag";
    def_mat.m_depth_only_fragment_shader = "";
    def_mat.m_alphablend = true;
    settings.loadMaterial(def_mat);

    settings.m_shader_name = "ghost";
    settings.m_drawing_priority = (char)drawing_order;
    settings.m_pipeline_type = GVPT_GHOST_DEPTH;
    if (doDepthOnlyRenderingFirst() && m_deferred_layouts.empty())
    {
        m_graphics_pipelines["ghost"] = {};
        m_graphics_pipelines["ghost"].m_settings = settings;
        m_graphics_pipelines["ghost"].m_settings.m_vertex_description = {};
        m_graphics_pipelines["ghost"].m_pipelines[GVPT_GHOST_DEPTH] =
            dp_cache.at(def_mat.m_vertex_shader + def_mat.m_fragment_shader +
            "|cull");
        m_graphics_pipelines["ghost" SKINNING_PIPELINE] = {};
        m_graphics_pipelines["ghost" SKINNING_PIPELINE].m_settings = settings;
        m_graphics_pipelines["ghost" SKINNING_PIPELINE]
            .m_settings.m_vertex_description = {};
        m_graphics_pipelines["ghost" SKINNING_PIPELINE]
            .m_pipelines[GVPT_GHOST_DEPTH] = dp_cache.at(
            def_mat.m_skinning_vertex_shader + def_mat.m_fragment_shader +
            "|cull");
    }
    else
    {
        createPipeline(vk, settings, dp_cache);
    }

    def_mat.m_fragment_shader = "ghost.frag";
    def_mat.m_depth_write = false;
    settings.loadMaterial(def_mat);

    settings.m_pipeline_type = GVPT_TRANSPARENT;
    settings.m_depth_op = VK_COMPARE_OP_EQUAL;
    createPipeline(vk, settings, dp_cache);
    drawing_order = drawing_order + 1;
    settings.m_depth_op = VK_COMPARE_OP_LESS;

    bool has_displace = getGEConfig()->m_pbr && !m_deferred_layouts.empty() &&
        m_deferred_layouts[GVDFP_DISPLACE_COLOR] != VK_NULL_HANDLE;
    for (auto& p : GEMaterialManager::g_materials)
    {
        if (!p.second->isTransparent() || skip_material(p.second))
            continue;
        if (!getGEConfig()->m_pbr && !p.second->m_nonpbr_fallback.empty())
            continue;
        bool is_displace = p.first == "displace";
        if (!has_displace && is_displace)
            continue;
        settings.m_drawing_priority = (char)drawing_order;
        drawing_order = drawing_order + 1;
        settings.m_shader_name = p.first;
        settings.m_material = p.second;
        settings.m_pipeline_type =
            is_displace ? GVPT_DISPLACE_COLOR : GVPT_TRANSPARENT;
        createPipeline(vk, settings, dp_cache);
    }

    if (has_displace)
    {
        def_mat.m_alphablend = false;
        def_mat.m_fragment_shader = "displace_mask.frag";
        def_mat.m_backface_culling = false;
        settings.loadMaterial(def_mat);

        settings.m_shader_name = "displace";
        settings.m_pipeline_type = GVPT_DISPLACE_MASK;
        createPipeline(vk, settings, dp_cache);
    }

    def_mat.m_alphablend = false;
    def_mat.m_backface_culling = true;
    def_mat.m_skinning_vertex_shader = "";
    def_mat.m_vertex_shader = "fullscreen_quad.vert";
    def_mat.m_fragment_shader = "skybox.frag";
    settings.loadMaterial(def_mat);

    settings.m_pipeline_type = GVPT_SKYBOX;
    settings.m_custom_pl = m_skybox_layout;
    settings.m_depth_op = VK_COMPARE_OP_EQUAL;
    settings.m_vertex_description = {};
    settings.m_shader_name = "skybox";
    createPipeline(vk, settings, dp_cache);

    if (m_deferred_layouts.empty())
        return;

    def_mat.m_depth_test = false;
    def_mat.m_fragment_shader = "deferred_pbr.frag";
    settings.loadMaterial(def_mat);
    settings.m_pipeline_type = GVPT_DEFERRED_LIGHTING;
    settings.m_custom_pl = m_deferred_layouts[GVDFP_HDR];
    settings.m_shader_name = "deferred_pbr";
    createPipeline(vk, settings, dp_cache);

    def_mat.m_depth_test = true;
    def_mat.m_additive = true;
    def_mat.m_vertex_shader = "deferred_pointlight.vert";
    def_mat.m_fragment_shader = "deferred_pointlight.frag";
    settings.loadMaterial(def_mat);
    settings.m_depth_op = VK_COMPARE_OP_LESS;
    settings.m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    settings.m_shader_name = "deferred_pointlight";
    createPipeline(vk, settings, dp_cache);

    def_mat.m_depth_test = false;
    def_mat.m_additive = false;
    def_mat.m_vertex_shader = "fullscreen_quad.vert";
    def_mat.m_fragment_shader = "deferred_convert_color.frag";
    settings.loadMaterial(def_mat);
    settings.m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    settings.m_shader_name = "deferred_convert_color";
    settings.m_pipeline_type = GVPT_DEFERRED_CONVERT_COLOR;
    settings.m_custom_pl = m_deferred_layouts[GVDFP_CONVERT_COLOR];
    createPipeline(vk, settings, dp_cache);

    if (has_displace)
    {
        def_mat.m_fragment_shader = "displace_color.frag";
        settings.loadMaterial(def_mat);
        settings.m_shader_name = "displace_color";
        settings.m_pipeline_type = GVPT_DISPLACE_COLOR;
        settings.m_custom_pl = m_deferred_layouts[GVDFP_DISPLACE_COLOR];
        createPipeline(vk, settings, dp_cache);
    }

    if (m_shadow_render_pass != VK_NULL_HANDLE)
        createShadowPipelines(vk);
    if (has_displace)
        createGlowPipelines(vk);
}   // createAllPipelines

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::createShadowResources(GEVulkanDriver* vk)
{
    // Sun shadow mapping needs the deferred PBR pipeline (the shadow factor
    // is applied in deferred_pbr.frag). The map is created even when shadows
    // are currently disabled so the data descriptor bindings stay valid; the
    // render pass is simply skipped then.
    if (!getGEConfig()->m_pbr || !vk->getRTTTexture() ||
        !vk->getRTTTexture()->isDeferredFBO())
        return;

    int size = getGEConfig()->m_shadow_map_size;
    if (size <= 0)
        size = 512;
    size = std::min(std::max(size, 256), 4096);
    // 2x1 atlas: near cascade in the left half, far cascade in the right
    irr::core::dimension2du dim(size * 2, size);
    m_shadow_map = GEVulkanAttachmentTexture::createDepthTexture(vk, dim,
        false/*lazy_allocation*/);

    // Make sure the first frame can sample it even if the shadow pass has
    // not run yet.
    VkCommandBuffer command_buffer =
        GEVulkanCommandLoader::beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_shadow_map->getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &barrier);
    GEVulkanCommandLoader::endSingleTimeCommands(command_buffer);

    // Depth-only render pass: cleared each frame, sampled by the deferred
    // lighting subpass of the main render pass afterwards.
    VkAttachmentDescription depth_desc = {};
    depth_desc.format = m_shadow_map->getInternalFormat();
    depth_desc.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_reference =
        { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_reference;

    std::array<VkSubpassDependency, 2> dependencies = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &depth_desc;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = dependencies.size();
    render_pass_info.pDependencies = dependencies.data();

    if (vkCreateRenderPass(vk->getDevice(), &render_pass_info, NULL,
        &m_shadow_render_pass) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass failed for shadow map");

    VkImageView attachment = (VkImageView)m_shadow_map->getTextureHandler();
    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = m_shadow_render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &attachment;
    framebuffer_info.width = dim.Width;
    framebuffer_info.height = dim.Height;
    framebuffer_info.layers = 1;
    if (vkCreateFramebuffer(vk->getDevice(), &framebuffer_info, NULL,
        &m_shadow_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("vkCreateFramebuffer failed for shadow map");
}   // createShadowResources

// ----------------------------------------------------------------------------
// Depth-only pipelines targeting the shadow render pass. Mirrors the
// GVPT_DEPTH pre-pass variants (same vertex/tessellation/depth fragment
// shaders) with a polygon depth bias against acne.
void GEVulkanDrawCall::createShadowPipelines(GEVulkanDriver* vk)
{
    const bool supports_tess =
        vk->getPhysicalDeviceFeatures().tessellationShader == VK_TRUE;
    std::unordered_map<std::string, std::shared_ptr<VkPipeline> > cache;

    VkPipelineShaderStageCreateInfo stage_info = {};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VertexDescription vertex_desc = getDefaultVertexDescription();
    vertex_input_info.vertexBindingDescriptionCount =
        vertex_desc.first.size();
    vertex_input_info.vertexAttributeDescriptionCount =
        vertex_desc.second.size();
    vertex_input_info.pVertexBindingDescriptions = vertex_desc.first.data();
    vertex_input_info.pVertexAttributeDescriptions = vertex_desc.second.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellation_state = {};
    tessellation_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation_state.patchControlPoints = 3;

    VkViewport viewport = {};
    viewport.width = (float)m_shadow_map->getSize().Width;
    viewport.height = (float)m_shadow_map->getSize().Height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = { m_shadow_map->getSize().Width,
        m_shadow_map->getSize().Height };
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 2.0f;
    rasterizer.depthBiasSlopeFactor = 4.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 0;

    std::array<VkDynamicState, 2> dynamic_state =
    {{
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VIEWPORT
    }};
    VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
    dynamic_state_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = dynamic_state.size();
    dynamic_state_info.pDynamicStates = dynamic_state.data();

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = m_shadow_render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    for (auto& p : GEMaterialManager::g_materials)
    {
        const std::shared_ptr<const GEMaterial>& m = p.second;
        if (m->isTransparent() ||
            m->m_depth_only_fragment_shader.empty())
            continue;
        if (!getGEConfig()->m_pbr && !m->m_nonpbr_fallback.empty())
            continue;
        const bool has_tessellation = supports_tess &&
            !m->m_tesc_shader.empty() && !m->m_tese_shader.empty();
        if (!supports_tess &&
            !m->m_tesc_shader.empty() && !m->m_tese_shader.empty())
            continue;
        if (m_graphics_pipelines.find(p.first) == m_graphics_pipelines.end())
            continue;

        auto build = [&](bool skinning) -> std::shared_ptr<VkPipeline>
        {
            const std::string& vs = skinning ?
                m->m_skinning_vertex_shader : m->m_vertex_shader;
            if (vs.empty())
                return nullptr;
            const bool use_tess = has_tessellation && !skinning;
            std::string key = vs + m->m_depth_only_fragment_shader +
                (use_tess ? m->m_tesc_shader + m->m_tese_shader :
                std::string()) +
                (m->m_backface_culling ? "|cull" : "|nocull");
            auto it = cache.find(key);
            if (it != cache.end())
                return it->second;

            std::vector<VkPipelineShaderStageCreateInfo> stages;
            stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
            stage_info.module = GEVulkanShaderManager::getShader(vs);
            stages.push_back(stage_info);
            stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stage_info.module = GEVulkanShaderManager::getShader(
                m->m_depth_only_fragment_shader);
            stages.push_back(stage_info);
            if (use_tess)
            {
                stage_info.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                stage_info.module = GEVulkanShaderManager::getShader(
                    m->m_tesc_shader);
                stages.push_back(stage_info);
                stage_info.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                stage_info.module = GEVulkanShaderManager::getShader(
                    m->m_tese_shader);
                stages.push_back(stage_info);
            }
            pipeline_info.stageCount = stages.size();
            pipeline_info.pStages = stages.data();
            pipeline_info.pTessellationState =
                use_tess ? &tessellation_state : NULL;
            input_assembly.topology = use_tess ?
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST :
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            rasterizer.cullMode = m->m_backface_culling ?
                VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;

            VkPipeline pipeline = VK_NULL_HANDLE;
            if (vk->createGraphicsPipelines(1, &pipeline_info, &pipeline) !=
                VK_SUCCESS)
            {
                throw std::runtime_error(
                    "vkCreateGraphicsPipelines failed for shadow " + p.first);
            }
            auto sp = std::shared_ptr<VkPipeline>(new VkPipeline(pipeline),
                destroyPipeline);
            cache[key] = sp;
            return sp;
        };

        auto pipeline = build(false);
        if (pipeline)
            m_graphics_pipelines[p.first].m_pipelines[GVPT_SHADOW] = pipeline;
        if (!m->m_skinning_vertex_shader.empty())
        {
            std::string sk = p.first + SKINNING_PIPELINE;
            if (m_graphics_pipelines.find(sk) != m_graphics_pipelines.end())
            {
                auto sk_pipeline = build(true);
                if (sk_pipeline)
                {
                    m_graphics_pipelines[sk].m_pipelines[GVPT_SHADOW] =
                        sk_pipeline;
                }
            }
        }
    }
}   // createShadowPipelines

// ----------------------------------------------------------------------------
// One shared pipeline pair (static + skinning) drawing per-object glow
// silhouettes into the glow attachment of the displace mask pass. Registered
// for every solid spm-based material so the glow pass can reuse the existing
// draw commands.
void GEVulkanDrawCall::createGlowPipelines(GEVulkanDriver* vk)
{
    auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
    if (!dfbo || !dfbo->getAttachment<GVDFT_GLOW>())
        return;

    VkPipelineShaderStageCreateInfo stage_info = {};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.pName = "main";
    std::vector<VkPipelineShaderStageCreateInfo> stages(2, stage_info);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = GEVulkanShaderManager::getShader("ge_glow.vert");
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = GEVulkanShaderManager::getShader("ge_glow.frag");

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VertexDescription vertex_desc = getDefaultVertexDescription();
    vertex_input_info.vertexBindingDescriptionCount =
        vertex_desc.first.size();
    vertex_input_info.vertexAttributeDescriptionCount =
        vertex_desc.second.size();
    vertex_input_info.pVertexBindingDescriptions = vertex_desc.first.data();
    vertex_input_info.pVertexAttributeDescriptions = vertex_desc.second.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.width = (float)vk->getSwapChainExtent().width;
    viewport.height = (float)vk->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = vk->getSwapChainExtent();
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Test (but don't write) against the scene depth so occluded glow
    // objects don't outline through walls. LEQUAL tolerates the slight
    // difference against depth laid down by the tessellated variants.
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Write only the glow attachment (the last colour attachment of the
    // displace mask pass); the displace mask / SSR attachments keep their
    // contents.
    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment(
        vk->getRTTTexture()->getZeroClearCountForPass(GVDFP_DISPLACE_MASK));
    for (auto& a : color_blend_attachment)
    {
        a = {};
        a.colorWriteMask = 0;
        a.blendEnable = VK_FALSE;
    }
    color_blend_attachment.back().colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = color_blend_attachment.size();
    color_blending.pAttachments = color_blend_attachment.data();

    std::array<VkDynamicState, 2> dynamic_state =
    {{
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VIEWPORT
    }};
    VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
    dynamic_state_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = dynamic_state.size();
    dynamic_state_info.pDynamicStates = dynamic_state.data();

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = stages.size();
    pipeline_info.pStages = stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass =
        vk->getRTTTexture()->getRTTRenderPass(GVDFP_DISPLACE_MASK);
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vk->createGraphicsPipelines(1, &pipeline_info, &pipeline) !=
        VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines failed for glow");
    auto glow = std::shared_ptr<VkPipeline>(new VkPipeline(pipeline),
        destroyPipeline);

    stages[0].module = GEVulkanShaderManager::getShader(
        "ge_glow_skinning.vert");
    if (vk->createGraphicsPipelines(1, &pipeline_info, &pipeline) !=
        VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkCreateGraphicsPipelines failed for glow skinning");
    }
    auto glow_skinning = std::shared_ptr<VkPipeline>(new VkPipeline(pipeline),
        destroyPipeline);

    // Attach to every solid spm-based pipeline so renderPipeline(GVPT_GLOW)
    // can replay the existing draw commands.
    for (auto& p : GEMaterialManager::g_materials)
    {
        const std::shared_ptr<const GEMaterial>& m = p.second;
        if (m->isTransparent())
            continue;
        if (m->m_vertex_shader != "spm.vert" &&
            m->m_vertex_shader != "spm_tess.vert")
            continue;
        auto it = m_graphics_pipelines.find(p.first);
        if (it != m_graphics_pipelines.end())
            it->second.m_pipelines[GVPT_GLOW] = glow;
        if (!m->m_skinning_vertex_shader.empty())
        {
            it = m_graphics_pipelines.find(p.first + SKINNING_PIPELINE);
            if (it != m_graphics_pipelines.end())
                it->second.m_pipelines[GVPT_GLOW] = glow_skinning;
        }
    }
}   // createGlowPipelines

// ----------------------------------------------------------------------------
size_t GEVulkanDrawCall::getShadowCameraOffset(unsigned cascade) const
{
    size_t ubo_size = sizeof(GEVulkanCameraUBO);
    size_t aligned = ubo_size +
        getPadding(ubo_size, m_limits.minUniformBufferOffsetAlignment);
    return aligned * (1 + cascade);
}   // getShadowCameraOffset

// ----------------------------------------------------------------------------
// Fits two orthographic sun frusta (near and far cascade) around the camera
// frustum, fills the shadow camera UBOs (used by the GVPT_SHADOW pass into
// the 2x1 atlas) and the player camera's shadow sampling parameters.
void GEVulkanDrawCall::updateSunShadowCamera(GEVulkanCameraSceneNode* cam)
{
    const bool enabled = m_shadow_map != NULL &&
        getGEConfig()->m_shadow_map_size > 0 && m_light_handler != NULL;
    float params[2][4] = {};
    irr::core::matrix4 sample_matrix[2];
    if (!enabled)
    {
        cam->setSunShadowData(sample_matrix[0], params[0], sample_matrix[1],
            params[1]);
        return;
    }

    // Cascade split and total distance covered along the view direction
    // (the far boundary matches the second SP/OpenGL cascade boundary).
    const float SPLIT = 16.0f;
    const float SHADOW_RANGE = 60.0f;

    const GEGlobalLightBuffer* light =
        (const GEGlobalLightBuffer*)m_light_handler->getData();
    irr::core::vector3df sun_dir = light->m_sun_direction; // towards the sun
    if (sun_dir.getLengthSQ() < 0.0001f)
        sun_dir = irr::core::vector3df(0.0f, 1.0f, 0.0f);
    sun_dir.normalize();

    const irr::core::matrix4& proj =
        cam->getViewFrustum()->getTransform(irr::video::ETS_PROJECTION);
    irr::core::matrix4 inv_view;
    cam->getViewFrustum()->getTransform(irr::video::ETS_VIEW)
        .getInverse(inv_view);
    float tx = proj[0] != 0.0f ? 1.0f / proj[0] : 1.0f;
    float ty = proj[5] != 0.0f ? 1.0f / proj[5] : 1.0f;

    irr::core::vector3df up = std::abs(sun_dir.Y) > 0.99f ?
        irr::core::vector3df(1.0f, 0.0f, 0.0f) :
        irr::core::vector3df(0.0f, 1.0f, 0.0f);

    // Square per-cascade resolution (the atlas is 2x1).
    const float res = (float)m_shadow_map->getSize().Height;

    for (unsigned c = 0; c < 2; c++)
    {
        // The far cascade starts inside the near one so the shader can blend
        // across the split without sampling outside either cascade.
        float z0 = c == 0 ? cam->getNearValue() : SPLIT * 0.75f;
        float z1 = c == 0 ? SPLIT :
            std::min(SHADOW_RANGE, cam->getFarValue());

        // Camera sub-frustum corners (unwarped matrices).
        std::array<irr::core::vector3df, 8> corners;
        unsigned corner_count = 0;
        for (float z : { z0, z1 })
        {
            for (float sx : { -1.0f, 1.0f })
            {
                for (float sy : { -1.0f, 1.0f })
                {
                    irr::core::vector3df p(sx * tx * z, sy * ty * z, z);
                    inv_view.transformVect(p);
                    corners[corner_count++] = p;
                }
            }
        }
        irr::core::vector3df center(0.0f, 0.0f, 0.0f);
        for (auto& p : corners)
            center += p;
        center /= (float)corner_count;

        irr::core::matrix4 light_view;
        light_view.buildCameraLookAtMatrixLH(center + sun_dir * 200.0f,
            center, up);

        // Light-space bounding box of the sub-frustum corners.
        irr::core::vector3df min_l, max_l;
        for (unsigned i = 0; i < corner_count; i++)
        {
            irr::core::vector3df p = corners[i];
            light_view.transformVect(p);
            if (i == 0)
                min_l = max_l = p;
            else
            {
                min_l.X = std::min(min_l.X, p.X);
                max_l.X = std::max(max_l.X, p.X);
                min_l.Y = std::min(min_l.Y, p.Y);
                max_l.Y = std::max(max_l.Y, p.Y);
                min_l.Z = std::min(min_l.Z, p.Z);
                max_l.Z = std::max(max_l.Z, p.Z);
            }
        }
        // Room for casters between the sun and the visible volume.
        const float CASTER_MARGIN = 150.0f;
        float zn = min_l.Z - CASTER_MARGIN;
        float zf = max_l.Z + 10.0f;
        float w = std::max(max_l.X - min_l.X, 1.0f);
        float h = std::max(max_l.Y - min_l.Y, 1.0f);
        float cx = (min_l.X + max_l.X) * 0.5f;
        float cy = (min_l.Y + max_l.Y) * 0.5f;

        // Snap the ortho window to shadow texel multiples (anti-shimmer).
        float texel_x = w / res;
        float texel_y = h / res;
        cx = std::floor(cx / texel_x) * texel_x;
        cy = std::floor(cy / texel_y) * texel_y;

        // Off-centre orthographic projection straight into Vulkan clip
        // space (x,y in [-1,1], z in [0,1]).
        irr::core::matrix4 ortho;
        float* o = ortho.pointer();
        memset(o, 0, sizeof(float) * 16);
        o[0] = 2.0f / w;
        o[5] = 2.0f / h;
        o[10] = 1.0f / (zf - zn);
        o[12] = -2.0f * cx / w;
        o[13] = -2.0f * cy / h;
        o[14] = -zn / (zf - zn);
        o[15] = 1.0f;

        irr::core::matrix4 sun_pv = ortho * light_view;

        // The shadow camera UBO only needs the matrices used by the
        // depth-only vertex/tessellation path; the relativity blocks stay
        // zeroed (default) so geometry is rendered unwarped, and the warp
        // bubble is pushed far away so the adaptive tessellation collapses
        // to level 1.
        GEVulkanCameraUBO& ubo = m_shadow_camera_ubo[c];
        ubo.m_view_matrix = light_view;
        ubo.m_projection_matrix = ortho;
        ubo.m_projection_view_matrix = sun_pv;
        sun_pv.getInverse(ubo.m_inverse_projection_view_matrix);
        light_view.getInverse(ubo.m_inverse_view_matrix);
        ortho.getInverse(ubo.m_inverse_projection_matrix);
        ubo.m_viewport.UpperLeftCorner.X = 0.0f;
        ubo.m_viewport.UpperLeftCorner.Y = 0.0f;
        ubo.m_viewport.LowerRightCorner.X = res;
        ubo.m_viewport.LowerRightCorner.Y = res;
        ubo.m_screensize.UpperLeftCorner.X = res;
        ubo.m_screensize.UpperLeftCorner.Y = res;
        ubo.m_relativity_bubble[0] = 1.0e8f;
        ubo.m_relativity_bubble[1] = 1.0e8f;
        ubo.m_relativity_bubble[2] = 1.0e8f;

        // World -> shadow atlas UV (u in this cascade's half) + depth01 for
        // sampling in deferred_pbr.frag.
        irr::core::matrix4 uv_bias;
        float* b = uv_bias.pointer();
        memset(b, 0, sizeof(float) * 16);
        b[0] = 0.25f;
        b[5] = 0.5f;
        b[10] = 1.0f;
        b[12] = 0.25f + 0.5f * (float)c;
        b[13] = 0.5f;
        b[15] = 1.0f;
        sample_matrix[c] = uv_bias * sun_pv;

        // Near: [depth range (0 = disabled), pcss, texel uv (v axis),
        // penumbra uv per metre]; far: pcss slot carries the split distance.
        params[c][0] = zf - zn;
        params[c][1] = c == 0 ?
            (getGEConfig()->m_pcss ? 1.0f : 0.0f) : SPLIT;
        params[c][2] = 1.0f / res;
        // PCSS penumbra: shadow-map UV radius (v axis) per metre of
        // receiver-blocker separation, ~1 degree effective sun size.
        params[c][3] = 0.02f / w;
    }
    cam->setSunShadowData(sample_matrix[0], params[0], sample_matrix[1],
        params[1]);
}   // updateSunShadowCamera

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::renderShadowMap(GEVulkanDriver* vk, VkCommandBuffer cmd)
{
    if (m_data_layout == VK_NULL_HANDLE || m_cmds.empty() ||
        m_shadow_map == NULL || m_shadow_render_pass == VK_NULL_HANDLE ||
        getGEConfig()->m_shadow_map_size <= 0)
        return;

    // Descriptor sets are bound while recording this pass; make sure their
    // pending updates run now (renderDrawCalls calls this again later, which
    // is then a no-op).
    prepareRendering(vk);

    VkClearValue clear_value;
    clear_value.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_shadow_render_pass;
    render_pass_info.framebuffer = m_shadow_framebuffer;
    render_pass_info.renderArea.offset = { 0, 0 };
    render_pass_info.renderArea.extent = { m_shadow_map->getSize().Width,
        m_shadow_map->getSize().Height };
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_value;
    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    if (GEVulkanFeatures::supportsBindMeshTexturesAtOnce())
        bindAllMaterials(cmd);

    // Render the near and far cascade into the two halves of the atlas.
    const uint32_t res = m_shadow_map->getSize().Height;
    for (unsigned c = 0; c < 2; c++)
    {
        VkViewport vp = {};
        vp.x = (float)(c * res);
        vp.width = (float)res;
        vp.height = (float)res;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D scissor = {};
        scissor.offset.x = (int32_t)(c * res);
        scissor.extent = { res, res };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        m_shadow_cascade = c;
        bool rebind_base_vertex = true;
        renderPipeline(vk, cmd, GVPT_SHADOW, rebind_base_vertex);
    }
    m_shadow_cascade = 0;

    vkCmdEndRenderPass(cmd);
}   // renderShadowMap

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::createPipeline(GEVulkanDriver* vk,
                                      const PipelineSettings& settings,
       std::unordered_map<std::string, std::shared_ptr<VkPipeline> >& dp_cache)
{
    VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.pName = "main";

    // Use a vector so tessellation stages can be appended dynamically
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages =
    {
        vert_shader_stage_info,
        frag_shader_stage_info
    };

    bool has_tessellation = !settings.m_material->m_tesc_shader.empty() &&
                            !settings.m_material->m_tese_shader.empty();
    if (has_tessellation)
    {
        VkPipelineShaderStageCreateInfo tesc_stage = {};
        tesc_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tesc_stage.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        tesc_stage.pName = "main";
        tesc_stage.module = GEVulkanShaderManager::getShader(
            settings.m_material->m_tesc_shader);

        VkPipelineShaderStageCreateInfo tese_stage = {};
        tese_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tese_stage.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        tese_stage.pName = "main";
        tese_stage.module = GEVulkanShaderManager::getShader(
            settings.m_material->m_tese_shader);

        shader_stages.push_back(tesc_stage);
        shader_stages.push_back(tese_stage);
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto& vertex_desc = settings.m_vertex_description;
    if (!vertex_desc.first.empty())
    {
        vertex_input_info.vertexBindingDescriptionCount = vertex_desc.first.size();
        vertex_input_info.vertexAttributeDescriptionCount = vertex_desc.second.size();
        vertex_input_info.pVertexBindingDescriptions = vertex_desc.first.data();
        vertex_input_info.pVertexAttributeDescriptions = vertex_desc.second.data();
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    // Tessellation requires PATCH_LIST topology; other pipelines use whatever
    // the settings specify (usually TRIANGLE_LIST).
    input_assembly.topology = has_tessellation ?
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : settings.m_topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellation_state = {};
    tessellation_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation_state.patchControlPoints = 3; // triangular patches

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)vk->getSwapChainExtent().width;
    viewport.height = (float)vk->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = vk->getSwapChainExtent();

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = settings.m_material->m_backface_culling ?
        VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = settings.m_material->m_depth_test;
    depth_stencil.depthWriteEnable = settings.m_material->m_depth_write;
    depth_stencil.depthCompareOp = settings.m_depth_op;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment(1);
    color_blend_attachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment[0].blendEnable = settings.m_material->isTransparent();
    if (settings.m_material->m_alphablend)
    {
        color_blend_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
    }
    if (settings.m_material->m_additive)
    {
        color_blend_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
    }
    if (vk->getRTTTexture() && vk->getRTTTexture()->isDeferredFBO())
    {
        switch (settings.m_pipeline_type)
        {
        case GVPT_DEPTH:
        case GVPT_SOLID:
            color_blend_attachment.resize(vk->getRTTTexture()
                ->getZeroClearCountForPass(GVDFP_GBUFFER),
                color_blend_attachment[0]);
            break;
        case GVPT_DISPLACE_MASK:
        {
            color_blend_attachment.resize(vk->getRTTTexture()
                ->getZeroClearCountForPass(GVDFP_DISPLACE_MASK),
                color_blend_attachment[0]);
            // The glow attachment (last colour attachment of the mask pass)
            // is written by the dedicated GVPT_GLOW pipelines only.
            auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
            if (dfbo->getAttachment<GVDFT_GLOW>())
                color_blend_attachment.back().colorWriteMask = 0;
            break;
        }
        case GVPT_DISPLACE_COLOR:
            color_blend_attachment.resize(vk->getRTTTexture()
                ->getZeroClearCountForPass(GVDFP_DISPLACE_COLOR),
                color_blend_attachment[0]);
            break;
        default:
            break;
        }
    }
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = color_blend_attachment.size();
    color_blending.pAttachments = color_blend_attachment.data();
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    std::array<VkDynamicState, 2> dynamic_state =
    {{
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VIEWPORT
    }};

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    dynamic_state_info.dynamicStateCount = dynamic_state.size(),
    dynamic_state_info.pDynamicStates = dynamic_state.data();

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = shader_stages.size();
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pTessellationState = has_tessellation ? &tessellation_state : NULL;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = settings.m_custom_pl == VK_NULL_HANDLE ?
        m_pipeline_layout : settings.m_custom_pl;
    pipeline_info.renderPass = getRenderPassForPipelineCreation(vk,
        settings.m_pipeline_type);
    pipeline_info.subpass = getSubpassForPipelineCreation(vk,
        settings.m_pipeline_type);
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    struct Constants
    {
        VkBool32 m_ibl;
        float m_specular_levels_minus_one;
        VkBool32 m_deferred;
        VkBool32 m_skybox;
        VkBool32 m_ssr;
        uint32_t m_hiz_iterations;
    };
    Constants constants = {};
    constants.m_ibl = getGEConfig()->m_pbr && getGEConfig()->m_ibl &&
        GEVulkanFeatures::supportsComputeInMainQueue() &&
        m_skybox_renderer != NULL;
    float ts = GEVulkanEnvironmentMap::getSpecularEnvironmentMapSize().Width;
    constants.m_specular_levels_minus_one = std::floor(std::log2(ts));
    constants.m_deferred = !m_deferred_layouts.empty();
    constants.m_skybox = m_skybox_renderer != NULL;
    constants.m_ssr = getGEConfig()->m_screen_space_reflection_type !=
        GSSRT_DISABLED;
    if (m_hiz_depth)
    {
        switch (getGEConfig()->m_screen_space_reflection_type)
        {
        case GSSRT_HIZ400:
            constants.m_hiz_iterations = 400;
            break;
        case GSSRT_HIZ200:
            constants.m_hiz_iterations = 200;
            break;
        default:
            constants.m_hiz_iterations = 100;
            break;
        }
    }
    std::array<VkSpecializationMapEntry, 6> specialization_entries = {};
    specialization_entries[0].constantID = 0;
    specialization_entries[0].offset = offsetof(Constants, m_ibl);
    specialization_entries[0].size = sizeof(VkBool32);
    specialization_entries[1].constantID = 1;
    specialization_entries[1].offset = offsetof(Constants,
        m_specular_levels_minus_one);
    specialization_entries[1].size = sizeof(float);
    specialization_entries[2].constantID = 2;
    specialization_entries[2].offset = offsetof(Constants, m_deferred);
    specialization_entries[2].size = sizeof(VkBool32);
    specialization_entries[3].constantID = 3;
    specialization_entries[3].offset = offsetof(Constants, m_skybox);
    specialization_entries[3].size = sizeof(VkBool32);
    specialization_entries[4].constantID = 4;
    specialization_entries[4].offset = offsetof(Constants, m_ssr);
    specialization_entries[4].size = sizeof(VkBool32);
    specialization_entries[5].constantID = 5;
    specialization_entries[5].offset = offsetof(Constants, m_hiz_iterations);
    specialization_entries[5].size = sizeof(uint32_t);
    VkSpecializationInfo specialization_info = {};
    specialization_info.mapEntryCount = specialization_entries.size();
    specialization_info.pMapEntries = specialization_entries.data();
    specialization_info.dataSize = sizeof(Constants);
    specialization_info.pData = &constants;
    if (getGEConfig()->m_pbr)
    {
        for (auto& stage : shader_stages)
            stage.pSpecializationInfo = &specialization_info;
    }

    shader_stages[0].module = GEVulkanShaderManager::getShader(
        settings.m_material->m_vertex_shader);
    shader_stages[1].module = GEVulkanShaderManager::getShader(
        settings.m_material->m_fragment_shader);

    bool depth_only = false;
    std::string depth_only_fs = settings.m_material->m_depth_only_fragment_shader;
    if (!doDepthOnlyRenderingFirst())
        depth_only_fs = "";
    if (!depth_only_fs.empty())
    {
        depth_only = true;
        shader_stages[1].module = GEVulkanShaderManager::getShader(
            depth_only_fs);
    }

    // The depth-only pipeline cache key must include the culling mode:
    // otherwise a two-sided material reuses the back-culled depth pipeline
    // of an earlier material with the same shaders, its back faces never
    // get written in the depth pre-pass, and the colour pass (which uses
    // VK_COMPARE_OP_EQUAL) discards them.
    auto dp_cache_key = [](const PipelineSettings& s, bool skinning)
    {
        std::string vs = skinning ?
            s.m_material->m_skinning_vertex_shader :
            s.m_material->m_vertex_shader;
        const bool has_tessellation = !skinning &&
            !s.m_material->m_tesc_shader.empty() &&
            !s.m_material->m_tese_shader.empty();
        return vs + s.m_material->m_depth_only_fragment_shader +
            (has_tessellation ?
            s.m_material->m_tesc_shader + s.m_material->m_tese_shader :
            std::string()) +
            (s.m_material->m_backface_culling ? "|cull" : "|nocull");
    };

    auto insert_from_cache = [&dp_cache, &dp_cache_key, this](
        const PipelineSettings& s, bool skinning)
    {
        auto it = dp_cache.find(dp_cache_key(s, skinning));
        if (it == dp_cache.end())
            return false;
        std::string key = s.m_shader_name;
        if (skinning)
            key += SKINNING_PIPELINE;
        if (m_graphics_pipelines.find(key) == m_graphics_pipelines.end())
        {
            m_graphics_pipelines[key] = {};
            m_graphics_pipelines[key].m_settings = s;
            m_graphics_pipelines[key].m_settings.m_vertex_description = {};
        }
        m_graphics_pipelines[key].m_pipelines[GVPT_DEPTH] = it->second;
        return true;
    };

    auto insert_pipeline = [vk, &dp_cache, &dp_cache_key, this](VkPipeline p,
                                                 const PipelineSettings& s,
                                                 bool depth_only,
                                                 bool skinning)
    {
        std::string key = s.m_shader_name;
        if (skinning)
            key += SKINNING_PIPELINE;
        if (m_graphics_pipelines.find(key) == m_graphics_pipelines.end())
        {
            m_graphics_pipelines[key] = {};
            m_graphics_pipelines[key].m_settings = s;
            m_graphics_pipelines[key].m_settings.m_vertex_description = {};
        }
        if (depth_only)
        {
            auto sp = std::shared_ptr<VkPipeline>(new VkPipeline(p),
                destroyPipeline);
            m_graphics_pipelines[key].m_pipelines[GVPT_DEPTH] = sp;
            dp_cache[dp_cache_key(s, skinning)] = sp;
        }
        else
        {
            m_graphics_pipelines[key].m_pipelines[s.m_pipeline_type] =
                std::shared_ptr<VkPipeline>(new VkPipeline(p),
                destroyPipeline);
        }
    };

    VkPipeline graphics_pipeline;
    const std::string& shader_name = settings.m_shader_name;

    // Skinned meshes (karts, characters) are dense and animated; they are
    // drawn without the tessellation stages even for "_tess" material
    // variants (the skinning vertex shader outputs the fragment layout
    // directly, which would not match the TCS inputs).
    auto set_tess_enabled = [&](bool enabled)
    {
        pipeline_info.stageCount = enabled ? shader_stages.size() : 2;
        pipeline_info.pTessellationState =
            enabled ? &tessellation_state : NULL;
        input_assembly.topology = enabled ?
            VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : settings.m_topology;
    };

    if (!insert_from_cache(settings, false))
    {
        VkResult result = vk->createGraphicsPipelines(1, &pipeline_info,
            &graphics_pipeline);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateGraphicsPipelines failed for " +
                shader_name);
        }
        insert_pipeline(graphics_pipeline, settings, depth_only, false);
    }

    if (!settings.m_material->m_skinning_vertex_shader.empty() &&
        !insert_from_cache(settings, true))
    {
        shader_stages[0].module = GEVulkanShaderManager::getShader(
            settings.m_material->m_skinning_vertex_shader);
        set_tess_enabled(false);
        VkResult result = vk->createGraphicsPipelines(1, &pipeline_info,
            &graphics_pipeline);
        set_tess_enabled(has_tessellation);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateGraphicsPipelines failed for " +
                shader_name);
        }
        insert_pipeline(graphics_pipeline, settings, depth_only, true);
    }
    if (depth_only_fs.empty())
        return;

    depth_only = false;
    VkPipelineDepthStencilStateCreateInfo color_after_depth = depth_stencil;
    color_after_depth.depthWriteEnable = VK_FALSE;
    color_after_depth.depthCompareOp = VK_COMPARE_OP_EQUAL;
    pipeline_info.pDepthStencilState = &color_after_depth;
    shader_stages[0].module = GEVulkanShaderManager::getShader(
        settings.m_material->m_vertex_shader);
    shader_stages[1].module = GEVulkanShaderManager::getShader(
        settings.m_material->m_fragment_shader);

    VkResult result = vk->createGraphicsPipelines(1, &pipeline_info,
        &graphics_pipeline);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateGraphicsPipelines failed for " +
            shader_name);
    }
    insert_pipeline(graphics_pipeline, settings, depth_only, false);

    if (settings.m_material->m_skinning_vertex_shader.empty())
        return;

    shader_stages[0].module = GEVulkanShaderManager::getShader(
        settings.m_material->m_skinning_vertex_shader);
    set_tess_enabled(false);
    result = vk->createGraphicsPipelines(1, &pipeline_info,
        &graphics_pipeline);
    set_tess_enabled(has_tessellation);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateGraphicsPipelines failed for " +
            shader_name);
    }
    insert_pipeline(graphics_pipeline, settings, depth_only, true);
}   // createPipeline

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::createVulkanData()
{
    GEVulkanDriver* vk = getVKDriver();

    // m_data_layout
    VkDescriptorSetLayoutBinding camera_layout_binding = {};
    camera_layout_binding.binding = 0;
    camera_layout_binding.descriptorCount = 1;
    camera_layout_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    camera_layout_binding.pImmutableSamplers = NULL;
    camera_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                                     | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
                                     | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT
                                     | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding object_data_layout_binding = {};
    object_data_layout_binding.binding = 1;
    object_data_layout_binding.descriptorCount = 1;
    object_data_layout_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    object_data_layout_binding.pImmutableSamplers = NULL;
    object_data_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding skinning_layout_binding = {};
    skinning_layout_binding.binding = 2;
    skinning_layout_binding.descriptorCount = 1;
    skinning_layout_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    skinning_layout_binding.pImmutableSamplers = NULL;
    skinning_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding light_layout_binding = {};
    light_layout_binding.binding = 3;
    light_layout_binding.descriptorCount = 1;
    light_layout_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    light_layout_binding.pImmutableSamplers = NULL;
    light_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings =
    {
         camera_layout_binding,
         object_data_layout_binding,
         skinning_layout_binding,
         light_layout_binding
    };
    if (GEVulkanFeatures::supportsBindMeshTexturesAtOnce())
    {
        VkDescriptorSetLayoutBinding material_binding = {};
        material_binding.binding = 4;
        material_binding.descriptorCount = 1;
        material_binding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        material_binding.pImmutableSamplers = NULL;
        material_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings.push_back(material_binding);
    }

    // Sun shadow map samplers (deferred_pbr.frag): binding 5 with a compare
    // sampler for hardware PCF, binding 6 with a plain sampler for the PCSS
    // blocker search. Only written when the shadow map exists (deferred PBR);
    // shaders in other configurations never statically use them.
    VkDescriptorSetLayoutBinding shadow_pcf_binding = {};
    shadow_pcf_binding.binding = 5;
    shadow_pcf_binding.descriptorCount = 1;
    shadow_pcf_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadow_pcf_binding.pImmutableSamplers = NULL;
    shadow_pcf_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(shadow_pcf_binding);

    VkDescriptorSetLayoutBinding shadow_raw_binding = shadow_pcf_binding;
    shadow_raw_binding.binding = 6;
    bindings.push_back(shadow_raw_binding);

    // Full-res GTAO result (deferred_pbr.frag), or a white fallback when the
    // hidden Vulkan AO path is disabled.
    VkDescriptorSetLayoutBinding ao_binding = shadow_pcf_binding;
    ao_binding.binding = 7;
    bindings.push_back(ao_binding);

    VkDescriptorSetLayoutCreateInfo setinfo = {};
    setinfo.flags = 0;
    setinfo.pNext = NULL;
    setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setinfo.pBindings = bindings.data();
    setinfo.bindingCount = bindings.size();

    VkResult result = vkCreateDescriptorSetLayout(vk->getDevice(), &setinfo,
        NULL, &m_data_layout);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed for data "
            "layout");
    }

    // m_descriptor_pool
    std::vector<VkDescriptorPoolSize> sizes =
    {
        {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            (vk->getMaxFrameInFlight() + 1) * 2
        },
        {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            (vk->getMaxFrameInFlight() + 1) * 2
        }
    };
    if (GEVulkanFeatures::supportsBindMeshTexturesAtOnce())
        sizes.back().descriptorCount = (vk->getMaxFrameInFlight() + 1) * 3;
    // Sun shadow map samplers (bindings 5 and 6) + AO result (binding 7)
    sizes.push_back(
    {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        (vk->getMaxFrameInFlight() + 1) * 3
    });

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = vk->getMaxFrameInFlight() + 1;
    pool_info.poolSizeCount = sizes.size();
    pool_info.pPoolSizes = sizes.data();

    if (vkCreateDescriptorPool(vk->getDevice(), &pool_info, NULL,
        &m_descriptor_pool) != VK_SUCCESS)
        throw std::runtime_error("createDescriptorPool failed");

    // m_data_descriptor_sets
    unsigned set_size = vk->getMaxFrameInFlight() + 1;
    m_data_descriptor_sets.resize(set_size);
    m_data_descriptor_set_generations.resize(set_size, 0);
    std::vector<VkDescriptorSetLayout> data_layouts(
        m_data_descriptor_sets.size(), m_data_layout);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = data_layouts.size();
    alloc_info.pSetLayouts = data_layouts.data();

    if (vkAllocateDescriptorSets(vk->getDevice(), &alloc_info,
        m_data_descriptor_sets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("vkAllocateDescriptorSets failed for data "
            "layout");
    }

    // m_pipeline_layout
    std::vector<VkDescriptorSetLayout> all_layouts =
    {
        *m_texture_descriptor->getDescriptorSetLayout(),
        m_data_layout
    };
    if (getGEConfig()->m_pbr)
    {
        all_layouts.push_back(
            vk->getSkyBoxRenderer()->getEnvDescriptorSetLayout());
        if (vk->getRTTTexture() && vk->getRTTTexture()->isDeferredFBO())
        {
            auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
            if (dfbo->getAttachment<GVDFT_DISPLACE_COLOR>())
            {
                all_layouts.push_back(
                    dfbo->getDescriptorSetLayout(GVDFP_DISPLACE_COLOR));
            }
        }
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = all_layouts.size();
    pipeline_layout_info.pSetLayouts = all_layouts.data();

    VkPushConstantRange push_constant;
    push_constant.offset = 0;
    const VkPhysicalDeviceLimits& limit =
        vk->getPhysicalDeviceProperties().limits;
    push_constant.size = std::min(limit.maxPushConstantsSize, 128u);
    push_constant.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
    pipeline_layout_info.pPushConstantRanges = &push_constant;
    pipeline_layout_info.pushConstantRangeCount = 1;

    result = vkCreatePipelineLayout(vk->getDevice(), &pipeline_layout_info,
        NULL, &m_pipeline_layout);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkCreatePipelineLayout failed for m_pipeline_layout");
    }

    all_layouts.resize(2);
    all_layouts[0] = vk->getSkyBoxRenderer()->getEnvDescriptorSetLayout();
    pipeline_layout_info.setLayoutCount = all_layouts.size();
    pipeline_layout_info.pSetLayouts = all_layouts.data();
    result = vkCreatePipelineLayout(vk->getDevice(), &pipeline_layout_info,
        NULL, &m_skybox_layout);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkCreatePipelineLayout failed for m_skybox_layout");
    }

    if (vk->getRTTTexture() && vk->getRTTTexture()->isDeferredFBO())
    {
        m_deferred_layouts.resize(GVDFP_COUNT);
        all_layouts.resize(3);
        all_layouts[2] = vk->getSkyBoxRenderer()->getEnvDescriptorSetLayout();
    }
    for (unsigned i = 0; i < m_deferred_layouts.size(); i++)
    {
        all_layouts[0] = vk->getRTTTexture()->getDescriptorSetLayout(i);
        if (all_layouts[0] == VK_NULL_HANDLE)
            continue;
        pipeline_layout_info.setLayoutCount = all_layouts.size();
        pipeline_layout_info.pSetLayouts = all_layouts.data();
        if (vkCreatePipelineLayout(vk->getDevice(), &pipeline_layout_info,
            NULL, &m_deferred_layouts[i]) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "vkCreatePipelineLayout failed for m_deferred_layouts");
        }
    }
    createShadowResources(vk);
    createAllPipelines(vk);

    size_t extra_size = 0;
    const bool use_multidraw =
        GEVulkanFeatures::supportsBindMeshTexturesAtOnce();
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (use_multidraw)
    {
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        extra_size = 200 * sizeof(VkDrawIndexedIndirectCommand);
    }
    // Use VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // or a staging buffer when buffer is small
    m_dynamic_data = new GEVulkanDynamicBuffer(flags,
        extra_size + getLightDataOffset() + sizeof(GEGlobalLightBuffer),
        GEVulkanDriver::getMaxFrameInFlight() + 1,
        GEVulkanDynamicBuffer::supportsHostTransfer() ? 0 :
        GEVulkanDriver::getMaxFrameInFlight() + 1);

    flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    // Using VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // will be a lot slower when there are many objects (like particles)
    m_sbo_data = new GEVulkanDynamicBuffer(flags, getInitialSBOSize(),
        GEVulkanDriver::getMaxFrameInFlight() + 1, 0,
        false/*enable_host_transfer*/);
}   // createVulkanData

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::uploadDynamicData(GEVulkanDriver* vk,
                                         GEVulkanCameraSceneNode* cam,
                                         VkCommandBuffer custom_cmd)
{
    if (!m_dynamic_data)
        return;

    VkCommandBuffer cmd =
        custom_cmd ? custom_cmd : vk->getCurrentCommandBuffer();

    // https://github.com/google/filament/pull/3814
    // Need both vertex and fragment bit
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    // Fill the sun shadow matrices (consumed both by the camera UBO below
    // and by the shadow camera UBO appended after it).
    updateSunShadowCamera(cam);

    std::vector<std::pair<void*, size_t> > data_uploading;
    data_uploading.emplace_back((void*)cam->getUBOData(),
        sizeof(GEVulkanCameraUBO));

    size_t sbo_padding = getShadowCameraOffset(0) - sizeof(GEVulkanCameraUBO);
    if (sbo_padding > 0)
        data_uploading.emplace_back((void*)NULL, sbo_padding);
    // Sun camera UBOs (near + far cascade) for the GVPT_SHADOW pass, bound
    // with camera dynamic offsets of getShadowCameraOffset(cascade).
    for (unsigned c = 0; c < 2; c++)
    {
        data_uploading.emplace_back((void*)&m_shadow_camera_ubo[c],
            sizeof(GEVulkanCameraUBO));
        if (sbo_padding > 0)
            data_uploading.emplace_back((void*)NULL, sbo_padding);
    }
    if (m_light_handler)
    {
        data_uploading.emplace_back(m_light_handler->getData(),
            m_light_handler->getSize());
    }

    const bool use_multidraw =
        GEVulkanFeatures::supportsBindMeshTexturesAtOnce();
    if (use_multidraw)
    {
        for (auto& cmd : m_cmds)
        {
            data_uploading.emplace_back(
                (void*)&cmd.m_cmd, sizeof(VkDrawIndexedIndirectCommand));
        }
        dst_stage |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }
    int current_buffer_idx = vk->getCurrentBufferIdx();
    if (m_dynamic_data->setCurrentData(data_uploading, cmd,
        current_buffer_idx))
    {
        invalidateDataDescriptorSets();
    }

    const size_t whole_size = m_skinning_data_padded_size +
        m_object_data_padded_size + m_materials_padded_size;
    vmaFlushAllocation(vk->getVmaAllocator(),
        m_sbo_data->getHostMemory()[current_buffer_idx], 0,
        whole_size);

    if (!GEVulkanDynamicBuffer::supportsHostTransfer())
    {
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        if (use_multidraw)
            barrier.dstAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dst_stage, 0,
            1, &barrier, 0, NULL, 0, NULL);
    }
}   // uploadDynamicData

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::bindBaseVertex(GEVulkanDriver* vk, VkCommandBuffer cmd)
{
    GEVulkanMeshCache* mc = vk->getVulkanMeshCache();
    std::array<VkBuffer, 2> vertex_buffer =
    {{
        mc->getBuffer(),
        mc->getBuffer()
    }};
    std::array<VkDeviceSize, 2> offsets =
    {{
        0,
        mc->getSkinningVBOOffset()
    }};
    vkCmdBindVertexBuffers(cmd, 0, vertex_buffer.size(),
        vertex_buffer.data(), offsets.data());

    vkCmdBindIndexBuffer(cmd, mc->getBuffer(), mc->getIBOOffset(),
        VK_INDEX_TYPE_UINT16);
}   // bindBaseVertex

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::prepareRendering(GEVulkanDriver* vk)
{
    updateDataDescriptorSets(vk);
    m_texture_descriptor->updateDescriptor();
}   // prepareRendering

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::prepareViewport(GEVulkanDriver* vk,
                                       GEVulkanCameraSceneNode* cam,
                                       VkCommandBuffer cmd)
{
    VkViewport vp;
    float scale = getGEConfig()->m_render_scale;
    if (vk->getSeparateRTTTexture())
        scale = 1.0f;
    vp.x = cam->getViewPort().UpperLeftCorner.X * scale;
    vp.y = cam->getViewPort().UpperLeftCorner.Y * scale;
    vp.width = cam->getViewPort().getWidth() * scale;
    vp.height = cam->getViewPort().getHeight() * scale;
    vp.minDepth = 0;
    vp.maxDepth = 1.0f;
    vk->getRotatedViewport(&vp, true/*handle_rtt*/);
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor;
    scissor.offset.x = vp.x;
    scissor.offset.y = vp.y;
    scissor.extent.width = vp.width;
    scissor.extent.height = vp.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}   // prepareViewport

// ----------------------------------------------------------------------------
std::vector<uint32_t> GEVulkanDrawCall::getDefaultDynamicOffsets() const
{
    if (GEVulkanFeatures::supportsBindMeshTexturesAtOnce())
        return std::vector<uint32_t>(5, 0);
    else
        return std::vector<uint32_t>(4, 0);
}   // getDefaultDynamicOffsets

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::bindAllMaterials(VkCommandBuffer cmd)
{
    // A draw call that had no visible node yet has not run createVulkanData,
    // so its pipeline layout doesn't exist (this happens for one frame when
    // entering a race from the menus). Binding with a null layout crashes
    // inside the driver; renderPipeline guards the same way via
    // m_data_layout.
    if (m_pipeline_layout == VK_NULL_HANDLE)
        return;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline_layout, 0, 1,
        m_texture_descriptor->getDescriptorSet(), 0, NULL);
}   // bindAllMaterials

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::renderPipeline(GEVulkanDriver* vk, VkCommandBuffer cmd,
                                      GEVulkanPipelineType pt,
                                      bool& rebind_base_vertex)
{
    if (m_data_layout == VK_NULL_HANDLE || m_cmds.empty())
        return;

    int current_buffer_idx = vk->getCurrentBufferIdx();

    const bool use_base_vertex = GEVulkanFeatures::supportsBaseVertexRendering();
    const bool bind_mesh_textures = GEVulkanFeatures::supportsBindMeshTexturesAtOnce();

    VkPipeline prev_pipeline = VK_NULL_HANDLE;
    std::string cur_pipeline;
    std::unordered_set<std::string> processed_dynamic_spm;
    bool bound = false;

    int cur_mid = -1;
    std::vector<uint32_t> dynamic_offsets = getDefaultDynamicOffsets();
    // The shadow pass renders with the sun camera UBO of the cascade being
    // recorded (uploaded right after the main camera UBO).
    if (pt == GVPT_SHADOW)
        dynamic_offsets[0] = (uint32_t)getShadowCameraOffset(m_shadow_cascade);
    if (getGEConfig()->m_pbr)
    {
        switch (pt)
        {
        case GVPT_SOLID:
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipeline_layout, 2, 1,
                vk->getSkyBoxRenderer()->getEnvDescriptorSet(), 0, NULL);
            break;
        case GVPT_DISPLACE_MASK:
        {
            auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
            if (dfbo && dfbo->getAttachment<GVDFT_DISPLACE_COLOR>())
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_pipeline_layout, 2, 1,
                    vk->getSkyBoxRenderer()->getEnvDescriptorSet(), 0, NULL);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_pipeline_layout, 3, 1, m_hiz_depth ?
                    m_hiz_depth->getRenderingDescriptorSet() :
                    dfbo->getDescriptorSet(GVDFP_DISPLACE_MASK), 0, NULL);
            }
            break;
        }
        case GVPT_DISPLACE_COLOR:
        {
            auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
            if (dfbo && dfbo->getAttachment<GVDFT_DISPLACE_COLOR>())
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_pipeline_layout, 3, 1,
                    dfbo->getDescriptorSet(GVDFP_DISPLACE_COLOR), 0, NULL);
            }
            break;
        }
        default:
            break;
        }
    }
    if (bind_mesh_textures)
    {
        cur_pipeline = m_cmds[0].m_shader;
        size_t indirect_offset = getLightDataOffset();
        if (m_light_handler)
            indirect_offset += m_light_handler->getSize();
        const size_t indirect_size = sizeof(VkDrawIndexedIndirectCommand);
        unsigned draw_count = 0;
        VkBuffer indirect_buffer =
            GEVulkanDynamicBuffer::supportsHostTransfer() ?
            m_dynamic_data->getHostBuffer()[current_buffer_idx] :
            m_dynamic_data->getLocalBuffer()[current_buffer_idx];
        for (unsigned i = 0; i < m_cmds.size(); i++)
        {
            bool is_last_cmd = (i == m_cmds.size() - 1);
            bool pipeline_change =
                !is_last_cmd && m_cmds[i + 1].m_shader != cur_pipeline;
            draw_count++;
            if (pipeline_change || is_last_cmd)
            {
                bound = bindPipeline(cmd, cur_pipeline, &prev_pipeline, pt);
                auto it = m_dynamic_spm_buffers.find(
                    getDynamicBufferKey(cur_pipeline));
                if (it != m_dynamic_spm_buffers.end() &&
                    processed_dynamic_spm.find(it->first) ==
                        processed_dynamic_spm.end())
                {
                    for (auto& buf : it->second)
                    {
                        if (bound)
                        {
                            auto& dy_offsets = m_dyspmb_materials[buf.first];
                            dynamic_offsets[1] = dy_offsets.second;
                            rebind_base_vertex = true;
                            bindDataDescriptor(cmd, current_buffer_idx,
                                dynamic_offsets);
                            buf.first->drawDynamicVertexIndexBuffer(cmd,
                                current_buffer_idx);
                        }
                    }
                    processed_dynamic_spm.insert(it->first);
                }
                if (rebind_base_vertex)
                {
                    bindBaseVertex(vk, cmd);
                    rebind_base_vertex = false;
                }
                if (bound)
                {
                    dynamic_offsets[1] = m_dynamic_spm_padded_size;
                    dynamic_offsets[4] = m_materials_data[cur_pipeline].first;
                    bindDataDescriptor(cmd, current_buffer_idx,
                        dynamic_offsets);
                    if (pt == GVPT_GLOW)
                    {
                        // Only replay the batches that contain glowing nodes.
                        unsigned group_start = i + 1 - draw_count;
                        unsigned k = 0;
                        while (k < draw_count)
                        {
                            if (!m_cmds[group_start + k].m_glow)
                            {
                                k++;
                                continue;
                            }
                            unsigned run = k + 1;
                            while (run < draw_count &&
                                m_cmds[group_start + run].m_glow)
                                run++;
                            vkCmdDrawIndexedIndirect(cmd, indirect_buffer,
                                indirect_offset + k * indirect_size,
                                run - k, indirect_size);
                            k = run;
                        }
                    }
                    else
                    {
                        vkCmdDrawIndexedIndirect(cmd, indirect_buffer,
                            indirect_offset, draw_count, indirect_size);
                    }
                }
                indirect_offset += draw_count * indirect_size;
                if (!is_last_cmd)
                {
                    draw_count = 0;
                    cur_pipeline = m_cmds[i + 1].m_shader;
                }
            }
        }
    }
    else
    {
        for (unsigned i = 0; i < m_cmds.size(); i++)
        {
            if (pt == GVPT_GLOW && !m_cmds[i].m_glow)
                continue;
            const VkDrawIndexedIndirectCommand& cur_cmd = m_cmds[i].m_cmd;
            if (m_cmds[i].m_shader != cur_pipeline)
            {
                cur_pipeline = m_cmds[i].m_shader;
                bound = bindPipeline(cmd, cur_pipeline, &prev_pipeline, pt);
                auto it = m_dynamic_spm_buffers.find(
                    getDynamicBufferKey(cur_pipeline));
                if (it != m_dynamic_spm_buffers.end() &&
                    processed_dynamic_spm.find(it->first) ==
                        processed_dynamic_spm.end())
                {
                    for (auto& buf : it->second)
                    {
                        auto& dy_offsets = m_dyspmb_materials[buf.first];
                        int dy_mat = dy_offsets.first;
                        if (dy_mat != cur_mid)
                        {
                            cur_mid = dy_mat;
                            bindSingleMaterial(cmd, cur_pipeline, cur_mid, pt);
                        }
                        if (bound)
                        {
                            dynamic_offsets[1] = dy_offsets.second;
                            rebind_base_vertex = true;
                            bindDataDescriptor(cmd, current_buffer_idx,
                                dynamic_offsets);
                            buf.first->drawDynamicVertexIndexBuffer(cmd,
                                current_buffer_idx);
                        }
                    }
                    processed_dynamic_spm.insert(it->first);
                }
            }
            int mid = m_cmds[i].m_material_id;
            if (cur_mid != mid)
            {
                cur_mid = mid;
                bindSingleMaterial(cmd, cur_pipeline, cur_mid, pt);
            }
            if (bound)
            {
                if (use_base_vertex && rebind_base_vertex)
                {
                    bindBaseVertex(vk, cmd);
                    rebind_base_vertex = false;
                    dynamic_offsets[1] = m_dynamic_spm_padded_size;
                    bindDataDescriptor(cmd, current_buffer_idx,
                        dynamic_offsets);
                }
                if (!use_base_vertex)
                {
                    dynamic_offsets[1] = m_dynamic_spm_padded_size +
                        m_cmds[i].m_dynamic_offset;
                    bindDataDescriptor(cmd, current_buffer_idx,
                        dynamic_offsets);
                    m_cmds[i].m_mb->bindVertexIndexBuffer(cmd);
                }
                vkCmdDrawIndexed(cmd, cur_cmd.indexCount,
                    cur_cmd.instanceCount, cur_cmd.firstIndex,
                    cur_cmd.vertexOffset,
                    use_base_vertex ? cur_cmd.firstInstance : 0);
            }
        }
    }
    for (auto& p : m_dynamic_spm_buffers)
    {
        if (processed_dynamic_spm.find(p.first) !=
            processed_dynamic_spm.end())
            continue;
        std::string dy_pipeline = getShaderFromKey(p.first);
        bound = bindPipeline(cmd, dy_pipeline, &prev_pipeline, pt);
        for (auto& buf : p.second)
        {
            auto& dy_offsets = m_dyspmb_materials[buf.first];
            if (!bind_mesh_textures)
            {
                int dy_mat = dy_offsets.first;
                if (dy_mat != cur_mid)
                {
                    cur_mid = dy_mat;
                    bindSingleMaterial(cmd, dy_pipeline, cur_mid, pt);
                }
            }
            if (bound)
            {
                dynamic_offsets[1] = dy_offsets.second;
                rebind_base_vertex = true;
                bindDataDescriptor(cmd, current_buffer_idx,
                    dynamic_offsets);
                buf.first->drawDynamicVertexIndexBuffer(cmd,
                    current_buffer_idx);
            }
        }
    }
}   // renderPipeline

// ----------------------------------------------------------------------------
size_t GEVulkanDrawCall::getInitialSBOSize() const
{
    // Assume 50 bones per node
    size_t ret = m_skinning_nodes.size() * 50 * sizeof(irr::core::matrix4);
    const bool use_base_vertex =
        GEVulkanFeatures::supportsBaseVertexRendering();
    for (auto& p : m_visible_nodes)
    {
        for (auto& q : p.second)
        {
            unsigned visible_count = q.second.size();
            if (visible_count == 0)
                continue;
            for (auto& r : q.second)
            {
                if (r.second == PARTICLE_NODE)
                {
                    irr::scene::IParticleSystemSceneNode* pn =
                        static_cast<irr::scene::IParticleSystemSceneNode*>(
                        r.first);
                    const core::array<SParticle>& particles =
                        pn->getParticles();
                    unsigned ps = particles.size();
                    if (ps == 0)
                    {
                        visible_count--;
                        continue;
                    }
                    visible_count += ps - 1;
                }
            }
            if (!use_base_vertex)
                visible_count *= 2;
            ret += visible_count * sizeof(ObjectData);
        }
    }
    return ret * 2;
}   // getInitialSBOSize

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::invalidateDataDescriptorSets()
{
    m_data_descriptor_generation++;
    if (m_data_descriptor_generation == 0)
    {
        m_data_descriptor_generation = 1;
        std::fill(m_data_descriptor_set_generations.begin(),
            m_data_descriptor_set_generations.end(), 0);
    }
}   // invalidateDataDescriptorSets

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::updateDataDescriptorSets(GEVulkanDriver* vk)
{
    if (m_skinning_data_padded_size == 0 ||
        m_object_data_padded_size == 0 || m_data_descriptor_sets.empty())
        return;

    // Data descriptor sets follow the same maxFramesInFlight + 1 ring as the
    // buffers they describe. The previous use of this slot has retired before
    // it comes around again, so only this set can be updated without idling the
    // whole device. Other slices stay dirty until their own safe turn.
    unsigned i = vk->getCurrentBufferIdx();
    if (i >= m_data_descriptor_sets.size())
        i = 0;
    if (m_data_descriptor_set_generations[i] ==
        m_data_descriptor_generation)
        return;

    const bool bind_mesh_textures =
        GEVulkanFeatures::supportsBindMeshTexturesAtOnce();
        VkDescriptorBufferInfo ubo_info;
        ubo_info.buffer = GEVulkanDynamicBuffer::supportsHostTransfer() ?
            m_dynamic_data->getHostBuffer()[i] :
            m_dynamic_data->getLocalBuffer()[i];
        ubo_info.offset = 0;
        ubo_info.range = sizeof(GEVulkanCameraUBO);

        std::vector<VkWriteDescriptorSet> data_set;
        data_set.resize(3, {});
        data_set[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        data_set[0].dstSet = m_data_descriptor_sets[i];
        data_set[0].dstBinding = 0;
        data_set[0].dstArrayElement = 0;
        data_set[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        data_set[0].descriptorCount = 1;
        data_set[0].pBufferInfo = &ubo_info;

        VkDescriptorBufferInfo sbo_info_objects;
        sbo_info_objects.buffer = m_sbo_data->getHostBuffer()[i];
        sbo_info_objects.offset = m_skinning_data_padded_size;
        sbo_info_objects.range = m_object_data_padded_size;

        data_set[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        data_set[1].dstSet = m_data_descriptor_sets[i];
        data_set[1].dstBinding = 1;
        data_set[1].dstArrayElement = 0;
        data_set[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        data_set[1].descriptorCount = 1;
        data_set[1].pBufferInfo = &sbo_info_objects;

        VkDescriptorBufferInfo sbo_info_skinning;
        sbo_info_skinning.buffer =
            m_sbo_data->getHostBuffer()[i];
        sbo_info_skinning.offset = 0;
        sbo_info_skinning.range = m_skinning_data_padded_size;

        data_set[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        data_set[2].dstSet = m_data_descriptor_sets[i];
        data_set[2].dstBinding = 2;
        data_set[2].dstArrayElement = 0;
        data_set[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        data_set[2].descriptorCount = 1;
        data_set[2].pBufferInfo = &sbo_info_skinning;

        VkDescriptorBufferInfo sbo_info_light;
        if (m_light_handler != NULL)
        {
            sbo_info_light.buffer =
                GEVulkanDynamicBuffer::supportsHostTransfer() ?
                m_dynamic_data->getHostBuffer()[i] :
                m_dynamic_data->getLocalBuffer()[i];
            sbo_info_light.offset = getLightDataOffset();
            sbo_info_light.range = sizeof(GEGlobalLightBuffer);
            data_set.push_back({});
            VkWriteDescriptorSet& ds = data_set.back();
            ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ds.dstSet = m_data_descriptor_sets[i];
            ds.dstBinding = 3;
            ds.dstArrayElement = 0;
            ds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            ds.descriptorCount = 1;
            ds.pBufferInfo = &sbo_info_light;
        }

        VkDescriptorBufferInfo sbo_info_material;
        sbo_info_material.buffer =
            m_sbo_data->getHostBuffer()[i];
        sbo_info_material.offset = m_skinning_data_padded_size +
            m_object_data_padded_size;
        sbo_info_material.range = m_materials_padded_size;
        if (bind_mesh_textures)
        {
            data_set.push_back({});
            VkWriteDescriptorSet& ds = data_set.back();
            ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ds.dstSet = m_data_descriptor_sets[i];
            ds.dstBinding = 4;
            ds.dstArrayElement = 0;
            ds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            ds.descriptorCount = 1;
            ds.pBufferInfo = &sbo_info_material;
        }

        // Sun shadow map samplers: binding 5 (compare sampler for PCF) and
        // binding 6 (plain sampler for the PCSS blocker search).
        std::array<VkDescriptorImageInfo, 2> shadow_infos = {};
        if (m_shadow_map)
        {
            shadow_infos[0].imageLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            shadow_infos[0].imageView =
                (VkImageView)m_shadow_map->getTextureHandler();
            shadow_infos[0].sampler = vk->getSampler(GVS_SHADOW);
            shadow_infos[1] = shadow_infos[0];
            shadow_infos[1].sampler = vk->getSampler(GVS_NEAREST);
            for (unsigned b = 0; b < 2; b++)
            {
                data_set.push_back({});
                VkWriteDescriptorSet& ds = data_set.back();
                ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                ds.dstSet = m_data_descriptor_sets[i];
                ds.dstBinding = 5 + b;
                ds.dstArrayElement = 0;
                ds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                ds.descriptorCount = 1;
                ds.pImageInfo = &shadow_infos[b];
            }
        }

        // GTAO result (binding 7). Always write a valid descriptor; when AO
        // is disabled deferred_pbr.frag samples white/no-occlusion.
        VkDescriptorImageInfo ao_info = {};
        ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        GEVulkanTexture* ao_result = m_gtao_pass ?
            m_gtao_pass->getResult() : NULL;
        ao_info.imageView = ao_result ?
            (VkImageView)ao_result->getTextureHandler() :
            (VkImageView)vk->getWhiteTexture()->getTextureHandler();
        ao_info.sampler = vk->getSampler(GVS_2D_RENDER);
        data_set.push_back({});
        VkWriteDescriptorSet& ds = data_set.back();
        ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ds.dstSet = m_data_descriptor_sets[i];
        ds.dstBinding = 7;
        ds.dstArrayElement = 0;
        ds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ds.descriptorCount = 1;
        ds.pImageInfo = &ao_info;

        vkUpdateDescriptorSets(vk->getDevice(), data_set.size(),
            data_set.data(), 0, NULL);
    m_data_descriptor_set_generations[i] = m_data_descriptor_generation;
}   // updateDataDescriptor

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::addSkyBox(scene::ISceneNode* node)
{
    m_skybox_renderer = getVKDriver()->getSkyBoxRenderer();
    m_skybox_renderer->addSkyBox(node);
}   // addSkyBox

// ----------------------------------------------------------------------------
bool GEVulkanDrawCall::renderSkyBox(GEVulkanDriver* vk, VkCommandBuffer cmd)
{
    if (!m_skybox_renderer)
        return false;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_graphics_pipelines["skybox"].m_pipelines[GVPT_SKYBOX].get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_skybox_layout, 0, 1, m_skybox_renderer->getEnvDescriptorSet(), 0,
        NULL);
    int current_buffer_idx = vk->getCurrentBufferIdx();
    std::vector<uint32_t> dynamic_offsets = getDefaultDynamicOffsets();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_skybox_layout,
        1, 1, &m_data_descriptor_sets[current_buffer_idx],
        dynamic_offsets.size(), dynamic_offsets.data());
    vkCmdDraw(cmd, 3, 1, 0, 0);
    return true;
}   // renderSkyBox

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::renderDeferredLighting(GEVulkanDriver* vk,
                                              VkCommandBuffer cmd)
{
    if (m_deferred_layouts.empty())
        return;
    auto& pl = m_graphics_pipelines.at("deferred_pbr").m_pipelines;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pl[GVPT_DEFERRED_LIGHTING]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_HDR], 0, 1,
        vk->getRTTTexture()->getDescriptorSet(GVDFP_HDR), 0, NULL);
    int current_buffer_idx = vk->getCurrentBufferIdx();
    std::vector<uint32_t> dynamic_offsets = getDefaultDynamicOffsets();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_HDR],
        1, 1, &m_data_descriptor_sets[current_buffer_idx],
        dynamic_offsets.size(), dynamic_offsets.data());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_HDR], 2, 1,
        vk->getSkyBoxRenderer()->getEnvDescriptorSet(), 0, NULL);
    unsigned fullscreen_light = m_light_handler ?
        m_light_handler->getFullscreenLightCount() : 0;
    vkCmdPushConstants(cmd, m_deferred_layouts[GVDFP_HDR],
        VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(unsigned),
        &fullscreen_light);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (m_light_handler &&
        m_light_handler->getLightCount() - fullscreen_light > 0)
    {
        auto& pl = m_graphics_pipelines.at("deferred_pointlight")
            .m_pipelines;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            *pl[GVPT_DEFERRED_LIGHTING]);
        struct PushConstants
        {
            btQuaternion m_rotation;
            int m_fullscreen_light;
        };
        PushConstants pc;
        pc.m_rotation = m_billboard_rotation;
        pc.m_fullscreen_light = fullscreen_light;
        vkCmdPushConstants(cmd, m_deferred_layouts[GVDFP_HDR],
            VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(PushConstants),
            &pc);
        vkCmdDraw(cmd, 4,
            m_light_handler->getLightCount() - fullscreen_light, 0, 0);
    }
}   // renderDeferredLighting

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::renderDeferredConvertColor(GEVulkanDriver* vk,
                                                  VkCommandBuffer cmd)
{
    if (m_deferred_layouts.empty())
        return;
    auto& pl = m_graphics_pipelines.at("deferred_convert_color").m_pipelines;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pl[GVPT_DEFERRED_CONVERT_COLOR]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_CONVERT_COLOR], 0, 1,
        vk->getRTTTexture()->getDescriptorSet(GVDFP_CONVERT_COLOR), 0, NULL);
    // Bind camera data (set=1) so the fragment shader can read relativity
    // parameters for Doppler colour shift.
    int current_buffer_idx = vk->getCurrentBufferIdx();
    std::vector<uint32_t> dynamic_offsets = getDefaultDynamicOffsets();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_CONVERT_COLOR],
        1, 1, &m_data_descriptor_sets[current_buffer_idx],
        dynamic_offsets.size(), dynamic_offsets.data());
    vkCmdDraw(cmd, 3, 1, 0, 0);
}   // renderDeferredConvertColor

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::renderDisplaceColor(GEVulkanDriver* vk,
                                           VkCommandBuffer cmd,
                                           VkBool32 has_displace)
{
    if (m_deferred_layouts.empty())
        return;
    auto& pl = m_graphics_pipelines.at("displace_color").m_pipelines;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pl[GVPT_DISPLACE_COLOR]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_DISPLACE_COLOR], 0, 1,
        vk->getRTTTexture()->getDescriptorSet(GVDFP_DISPLACE_COLOR), 0, NULL);
    int current_buffer_idx = vk->getCurrentBufferIdx();
    std::vector<uint32_t> dynamic_offsets = getDefaultDynamicOffsets();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_deferred_layouts[GVDFP_DISPLACE_COLOR],
        1, 1, &m_data_descriptor_sets[current_buffer_idx],
        dynamic_offsets.size(), dynamic_offsets.data());
    vkCmdPushConstants(cmd, m_deferred_layouts[GVDFP_DISPLACE_COLOR],
        VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(VkBool32), &has_displace);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}   // renderDisplaceColor

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::bindSingleMaterial(VkCommandBuffer cmd,
                                          const std::string& cur_pipeline,
                                          int material_id,
                                          GEVulkanPipelineType pt)
{
    const PipelineData& data = m_graphics_pipelines.at(cur_pipeline);
    if (data.m_pipelines.find(pt) == data.m_pipelines.end())
        return;
    const PipelineSettings& s = data.m_settings;
    if (pt == GVPT_GHOST_DEPTH ||
        ((pt == GVPT_DEPTH || pt == GVPT_SHADOW) &&
        s.m_material->texturelessDepth()))
        return;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline_layout, 0, 1,
        &m_texture_descriptor->getDescriptorSet()[material_id], 0, NULL);
}   // bindSingleMaterial

// ----------------------------------------------------------------------------
bool GEVulkanDrawCall::doDepthOnlyRenderingFirst()
{
    enum Status
    {
        UNDEFINED,
        ENABLED,
        DISABLED_NOT_PBR,
        DISABLED_TILED_GPU,
    };
    static Status status = UNDEFINED;
    auto ret = []()
    {
        if (!getGEConfig()->m_pbr)
            return DISABLED_NOT_PBR;
        // https://developer.arm.com/documentation/101897/0304/Optimizing-application-logic/Avoid-using-depth-prepasses
#if defined(TILED_GPU)
        return DISABLED_TILED_GPU;
#else
        return ENABLED;
#endif
    };
    Status prev_status = status;
    status = ret();
    if (prev_status != status)
    {
        switch (status)
        {
        case ENABLED:
            printf("Enabled depth prepass.\n");
            break;
        case DISABLED_NOT_PBR:
            printf("Disabled depth prepass because it will make non-PBR"
                " rendering slower.\n");
            break;
        case DISABLED_TILED_GPU:
            printf("Disabled depth prepass because it isn't necessary for"
                " tile-based GPU.\n");
            break;
        default:
            break;
        }
    }
    return status == ENABLED;
}   // doDepthOnlyRenderingFirst

// ----------------------------------------------------------------------------
VertexDescription GEVulkanDrawCall::getDefaultVertexDescription() const
{
    VertexDescription vertex_description;
    auto& binding_descriptions = vertex_description.first;
    binding_descriptions.resize(2);
    size_t bone_pitch = sizeof(int16_t) * 8;
    size_t static_pitch = sizeof(irr::video::S3DVertexSkinnedMesh) - bone_pitch;
    binding_descriptions[0].binding = 0;
    binding_descriptions[0].stride = static_pitch;
    binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    binding_descriptions[1].binding = 1;
    binding_descriptions[1].stride = bone_pitch;
    binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    auto& attribute_descriptions = vertex_description.second;
    attribute_descriptions.resize(8);
    attribute_descriptions[0].binding = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_position);
    attribute_descriptions[1].binding = 0;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    attribute_descriptions[1].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_normal);
    attribute_descriptions[2].binding = 0;
    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    attribute_descriptions[2].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_color);
    attribute_descriptions[3].binding = 0;
    attribute_descriptions[3].location = 3;
    attribute_descriptions[3].format = VK_FORMAT_R16G16_SFLOAT;
    attribute_descriptions[3].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_all_uvs);
    attribute_descriptions[4].binding = 0;
    attribute_descriptions[4].location = 4;
    attribute_descriptions[4].format = VK_FORMAT_R16G16_SFLOAT;
    attribute_descriptions[4].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_all_uvs) + (sizeof(int16_t) * 2);
    attribute_descriptions[5].binding = 0;
    attribute_descriptions[5].location = 5;
    attribute_descriptions[5].format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    attribute_descriptions[5].offset = offsetof(irr::video::S3DVertexSkinnedMesh, m_tangent);
    attribute_descriptions[6].binding = 1;
    attribute_descriptions[6].location = 6;
    attribute_descriptions[6].format = VK_FORMAT_R16G16B16A16_SINT;
    attribute_descriptions[6].offset = 0;
    attribute_descriptions[7].binding = 1;
    attribute_descriptions[7].location = 7;
    attribute_descriptions[7].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attribute_descriptions[7].offset = sizeof(int16_t) * 4;

    return vertex_description;
}   // getDefaultVertexDescription

// ----------------------------------------------------------------------------
void GEVulkanDrawCall::addLightNode(irr::scene::ILightSceneNode* node)
{
    if (!m_light_handler)
        return;
    if (node->getLightType() == irr::video::ELT_DIRECTIONAL)
    {
        // Sun node
        m_light_handler->addLightNode(node);
    }
    else
    {
        const video::SLight& l = node->getLightData();
        if (m_culling_tool->isCulled(l.Position, l.Radius))
            return;
        m_light_handler->addLightNode(node);
    }
}   // addLightNode

// ----------------------------------------------------------------------------
size_t GEVulkanDrawCall::getLightDataOffset() const
{
    // [camera][near sun camera][far sun camera][light data]
    return getShadowCameraOffset(2);
}   // getLightDataOffset

// ----------------------------------------------------------------------------
bool GEVulkanDrawCall::bindPipeline(VkCommandBuffer cmd,
                                    const std::string& name,
                                    VkPipeline* prev_pipeline,
                                    GEVulkanPipelineType pt) const
{
    auto& ret = m_graphics_pipelines.at(name);
    if (ret.m_pipelines.find(pt) == ret.m_pipelines.end())
        return false;
    VkPipeline p = *ret.m_pipelines.at(pt);
    if (*prev_pipeline == p)
        return true;
    *prev_pipeline = p;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    if (ret.m_settings.m_material->m_push_constants)
    {
        uint32_t size;
        void* data;
        ret.m_settings.m_material->m_push_constants(&size, &data);
        vkCmdPushConstants(cmd, m_pipeline_layout,
            VK_SHADER_STAGE_ALL_GRAPHICS, 0, size, data);
    }
    return true;
}   // bindPipeline

// ----------------------------------------------------------------------------
VkRenderPass GEVulkanDrawCall::getRenderPassForPipelineCreation(
                                                            GEVulkanDriver* vk,
                                                     GEVulkanPipelineType type)
{
    GEVulkanFBOTexture* fbo = vk->getRTTTexture();
    if (fbo)
    {
        if (fbo->getRTTRenderPassCount() == 1)
            return fbo->getRTTRenderPass();
        else
        {
            switch (type)
            {
            case GVPT_DEPTH:
            case GVPT_SOLID:
                return fbo->getRTTRenderPass(GVDFP_GBUFFER);
            case GVPT_DEFERRED_LIGHTING:
            case GVPT_SKYBOX:
                return fbo->getRTTRenderPass(GVDFP_HDR);
            case GVPT_DEFERRED_CONVERT_COLOR:
            case GVPT_GHOST_DEPTH:
            case GVPT_TRANSPARENT:
                return fbo->getRTTRenderPass(GVDFP_CONVERT_COLOR);
            case GVPT_DISPLACE_MASK:
                return fbo->getRTTRenderPass(GVDFP_DISPLACE_MASK);
            case GVPT_DISPLACE_COLOR:
                return fbo->getRTTRenderPass(GVDFP_DISPLACE_COLOR);
            default:
                return fbo->getRTTRenderPass(0);
            }
        }
    }
    else
        return vk->getRenderPass();
}   // getRenderPassForPipelineCreation

// ----------------------------------------------------------------------------
uint32_t GEVulkanDrawCall::getSubpassForPipelineCreation(
                                                            GEVulkanDriver* vk,
                                                     GEVulkanPipelineType type)
{
    if (vk->getRTTTexture() && vk->getRTTTexture()->isDeferredFBO())
    {
        auto* dfbo = static_cast<GEVulkanDeferredFBO*>(vk->getRTTTexture());
        if (dfbo->getAttachment<GVDFT_DISPLACE_COLOR>())
        {
            if (type == GVPT_DISPLACE_MASK || type == GVPT_DISPLACE_COLOR)
                return 0;
        }
        return 0;
    }
    return 0;
}   // getSubpassForPipelineCreation

}
