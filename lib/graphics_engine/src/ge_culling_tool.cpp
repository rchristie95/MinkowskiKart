#include "ge_culling_tool.hpp"

#include "ge_main.hpp"
#include "ge_spm_buffer.hpp"
#include "ge_vulkan_camera_scene_node.hpp"

#include "ISceneNode.h"

#include <algorithm>
#include <cmath>

namespace GE
{
// ----------------------------------------------------------------------------
void GECullingTool::init(GEVulkanCameraSceneNode* cam)
{
    m_camera = cam;
    mathPlaneFrustumf(&m_frustum[0].X, cam->getPVM());
    m_cam_bbox = cam->getViewFrustum()->getBoundingBox();
}   // init

// ----------------------------------------------------------------------------
bool GECullingTool::isCulled(const irr::core::vector3df& center, float radius)
{
    // Note: this overload (point lights) stays active even when
    // m_disable_frustum_culling is set for warped geometry — disabling it
    // overflows the per-frame light cap and makes lighting flicker.
    for (int i = 0; i < 6; i++)
    {
        irr::core::quaternion q(center.X, center.Y, center.Z, 1.0f);
        if (m_frustum[i].dotProduct(q) < -radius)
            return true;
    }
    return false;
}   // isCulled

// ----------------------------------------------------------------------------
bool GECullingTool::isBoxOutsideFrustum(
    const irr::core::aabbox3df& bb) const
{
    if (!m_cam_bbox.intersectsWithBox(bb))
        return true;

    using namespace irr;
    using namespace core;
    quaternion edges[8] =
    {
        quaternion(bb.MinEdge.X, bb.MinEdge.Y, bb.MinEdge.Z, 1.0f),
        quaternion(bb.MaxEdge.X, bb.MinEdge.Y, bb.MinEdge.Z, 1.0f),
        quaternion(bb.MinEdge.X, bb.MaxEdge.Y, bb.MinEdge.Z, 1.0f),
        quaternion(bb.MaxEdge.X, bb.MaxEdge.Y, bb.MinEdge.Z, 1.0f),
        quaternion(bb.MinEdge.X, bb.MinEdge.Y, bb.MaxEdge.Z, 1.0f),
        quaternion(bb.MaxEdge.X, bb.MinEdge.Y, bb.MaxEdge.Z, 1.0f),
        quaternion(bb.MinEdge.X, bb.MaxEdge.Y, bb.MaxEdge.Z, 1.0f),
        quaternion(bb.MaxEdge.X, bb.MaxEdge.Y, bb.MaxEdge.Z, 1.0f)
    };

    for (int i = 0; i < 6; i++)
    {
        bool culled = true;
        for (int j = 0; j < 8; j++)
        {
            if (m_frustum[i].dotProduct(edges[j]) >= 0.0)
            {
                culled = false;
                break;
            }
        }
        if (culled)
            return true;
    }
    return false;
}   // isBoxOutsideFrustum

// ----------------------------------------------------------------------------
bool GECullingTool::isWarpedStaticBoxCulled(
    const irr::core::aabbox3df& bb) const
{
    if (!m_camera)
        return false;

    const GEVulkanCameraUBO* ubo = m_camera->getUBOData();
    if (ubo->m_relativity_params[0] <= 0.5f)
        return isBoxOutsideFrustum(bb);

    using namespace irr::core;
    const vector3df observer(ubo->m_relativity_observer_pos[0],
        ubo->m_relativity_observer_pos[1],
        ubo->m_relativity_observer_pos[2]);
    const vector3df beta(ubo->m_relativity_beta[0],
        ubo->m_relativity_beta[1], ubo->m_relativity_beta[2]);
    const float beta2 = beta.getLengthSQ();
    const float gamma = std::max(ubo->m_relativity_params[2], 1.0f);
    if (beta2 < 1.0e-6f)
        return isBoxOutsideFrustum(bb);

    auto warp_point = [&](const vector3df& point)->vector3df
    {
        vector3df relative = point - observer;
        const float distance = relative.getLength();
        if (distance < 1.0e-4f)
            return point;
        const vector3df direction = relative / distance;
        const float beta_dot = beta.dotProduct(direction);
        const float denominator = 1.0f + beta_dot;
        if (fabsf(denominator) < 1.0e-5f)
            return point;
        vector3df observer_direction = direction / gamma +
            beta * (((gamma / (gamma + 1.0f)) * beta_dot) + 1.0f);
        observer_direction /= denominator;
        if (observer_direction.getLengthSQ() < 1.0e-8f)
            return point;
        observer_direction.normalize();
        return observer + observer_direction * distance;
    };

    // Enclose the source AABB in a sphere. Relativistic aberration preserves
    // radius from the observer and is conformal on the direction sphere. Its
    // worst angular magnification is sqrt((1+beta)/(1-beta)), which gives a
    // conservative cone bound for every point instead of relying on sampled
    // corners that can miss a high-gamma extremum.
    const vector3df center = bb.getCenter();
    const float radius = bb.getExtent().getLength() * 0.5f;
    const float center_distance = center.getDistanceFrom(observer);
    if (center_distance <= radius + 1.0e-4f)
        return false; // Observer lies in the bound: it can cover any direction.

    const float beta_length = sqrtf(beta2);
    if (beta_length >= 0.9999f)
        return false; // Avoid any finite bound near the aberration singularity.
    const float source_angle = asinf(std::min(radius / center_distance, 1.0f));
    const float max_magnification = sqrtf(
        (1.0f + beta_length) / (1.0f - beta_length));
    const float warped_angle = std::min(
        source_angle * max_magnification, 3.14159265358979323846f);
    const float warped_radius = radius +
        2.0f * (center_distance + radius) * sinf(warped_angle * 0.5f);
    const vector3df warped_center = warp_point(center);
    const vector3df padding(warped_radius, warped_radius, warped_radius);
    aabbox3df warped(warped_center - padding, warped_center + padding);
    return isBoxOutsideFrustum(warped);
}   // isWarpedStaticBoxCulled

// ----------------------------------------------------------------------------
bool GECullingTool::isCulled(irr::core::aabbox3df& bb)
{
    if (getGEConfig()->m_disable_frustum_culling)
        return false;
    return isBoxOutsideFrustum(bb);
}   // isCulled

// ----------------------------------------------------------------------------
bool GECullingTool::isCulled(GESPMBuffer* buffer,
                             irr::scene::ISceneNode* node,
                             unsigned material_index)
{
    irr::core::aabbox3df bb = buffer->getBoundingBox();
    node->getAbsoluteTransformation().transformBoxEx(bb);
    if (getGEConfig()->m_disable_frustum_culling)
    {
        float relativity_data[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        GENodeVelocityFunction velocity_func = getNodeVelocityFunction();
        if (velocity_func)
        {
            const irr::video::SMaterial* material =
                material_index < node->getMaterialCount() ?
                &node->getMaterial(material_index) : &buffer->getMaterial();
            velocity_func(node, material, relativity_data);
        }
        if (relativity_data[3] > 0.5f)
            return isBoxOutsideFrustum(bb);

        // Static track meshes have zero object velocity and can be mapped to
        // their apparent position exactly enough for conservative culling.
        // Keep moving/skinned/billboard geometry visible: its emission-time
        // displacement cannot be inferred from the unwarped AABB here.
        if (node->getType() == irr::scene::ESNT_MESH &&
            relativity_data[0] * relativity_data[0] +
                relativity_data[1] * relativity_data[1] +
                relativity_data[2] * relativity_data[2] < 1.0e-6f &&
            buffer->getHardwareMappingHint_Vertex() !=
                irr::scene::EHM_STREAM &&
            buffer->getHardwareMappingHint_Index() !=
                irr::scene::EHM_STREAM)
        {
            return isWarpedStaticBoxCulled(bb);
        }
        return false;
    }
    return isCulled(bb);
}   // isCulled

}
