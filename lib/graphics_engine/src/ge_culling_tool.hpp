#ifndef HEADER_GE_CULLING_TOOL_HPP
#define HEADER_GE_CULLING_TOOL_HPP

#include "aabbox3d.h"
#include "quaternion.h"
#include "matrix4.h"

namespace irr
{
    namespace scene { class ISceneNode; }
}

namespace GE
{
class GESPMBuffer;
class GEVulkanCameraSceneNode;

class GECullingTool
{
private:
    irr::core::quaternion m_frustum[6];

    irr::core::aabbox3df m_cam_bbox;

    GEVulkanCameraSceneNode* m_camera = nullptr;

    // Test a box that is already in the same world-space frame as the
    // frustum, bypassing the warped-geometry policy switch.
    bool isBoxOutsideFrustum(const irr::core::aabbox3df& bb) const;
    // Static geometry can be culled safely using a spherical angular bound
    // expanded by the maximum magnification of the aberration mapping.
    bool isWarpedStaticBoxCulled(const irr::core::aabbox3df& bb) const;
public:
    // ------------------------------------------------------------------------
    void init(GEVulkanCameraSceneNode* cam);
    // ------------------------------------------------------------------------
    bool isCulled(irr::core::aabbox3df& bb);
    // ------------------------------------------------------------------------
    bool isCulled(const irr::core::vector3df& center, float radius);
    // ------------------------------------------------------------------------
    bool isCulled(GESPMBuffer* buffer, irr::scene::ISceneNode* node,
                  unsigned material_index);
};   // GECullingTool

}

#endif
