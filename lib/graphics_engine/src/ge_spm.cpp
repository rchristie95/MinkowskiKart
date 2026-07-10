#include "ge_spm.hpp"

#include "ge_animation.hpp"
#include "ge_spm_buffer.hpp"

#include <algorithm>

namespace GE
{
GESPM::GESPM()
     : m_fps(0.0f), m_bind_frame(0), m_total_joints(0), m_joint_using(0),
       m_frame_count(0)
{
}   // GESPM

// ----------------------------------------------------------------------------
GESPM::~GESPM()
{
    for (unsigned i = 0; i < m_buffer.size(); i++)
        m_buffer[i]->drop();
}   // ~GESPM

// ----------------------------------------------------------------------------
IMeshBuffer* GESPM::getMeshBuffer(u32 nr) const
{
    if (nr < m_buffer.size())
        return m_buffer[nr];
    else
        return NULL;
}   // getMeshBuffer

// ----------------------------------------------------------------------------
IMeshBuffer* GESPM::getMeshBuffer(const video::SMaterial &material) const
{
    for (unsigned i = 0; i < m_buffer.size(); i++)
    {
        if (m_buffer[i]->getMaterial() == material)
            return m_buffer[i];
    }
    return NULL;
}   // getMeshBuffer

// ----------------------------------------------------------------------------
void GESPM::finalize()
{
    m_bounding_box.reset(0.0f, 0.0f, 0.0f);
    for (unsigned i = 0; i < m_buffer.size(); i++)
    {
#if defined(__APPLE__)
        // Apple/MoltenVK has no affordable GPU tessellation, so bake the
        // subdivision into coarse static geometry at load time; the per-vertex
        // relativistic warp (spm.vert) then renders it smoothly at full speed,
        // instead of the emulated per-frame tessellation that crawled at
        // ~0.3 fps. No-op for dense/skinned buffers.
        m_buffer[i]->subdivideForRelativity();
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || \
      defined(_M_ARM64)
        // Mobile keeps bounded GPU tessellation for the live relativistic
        // transform. Only single-buffer coarse meshes are pre-split: uniform
        // subdivision across separate material buffers can introduce a
        // T-junction where only one side of a shared edge takes another pass.
        // Large road/ocean sheets are normally single-buffer meshes, while
        // multi-material tracks retain the seam-safe shared-edge GPU path.
        if (m_buffer.size() == 1 && m_buffer[i]->getIndexCount() < 12000)
            m_buffer[i]->subdivideForRelativity(2.8f, 4);
#endif
        m_bounding_box.addInternalBox(m_buffer[i]->getBoundingBox());
        m_buffer[i]->createVertexIndexBuffer();
    }

    for (Armature& arm : getArmatures())
    {
        arm.getInterpolatedMatrices((float)m_bind_frame);
        for (auto& p : arm.m_world_matrices)
        {
            p.second = false;
        }
        for (unsigned i = 0; i < arm.m_joint_names.size(); i++)
        {
            core::matrix4 m;
            arm.getWorldMatrix(arm.m_interpolated_matrices, i).getInverse(m);
            arm.m_joint_matrices[i] = m;
        }
    }
}   // finalize

// ----------------------------------------------------------------------------
void GESPM::getSkinningMatrices(f32 frame, std::vector<core::matrix4>& dest,
                    float frame_interpolating, float rate)
{
    unsigned accumulated_joints = 0;
    for (unsigned i = 0; i < m_all_armatures.size(); i++)
    {
        m_all_armatures[i].getPose(frame, &dest[accumulated_joints],
            frame_interpolating, rate);
        accumulated_joints += m_all_armatures[i].m_joint_used;
    }
}   // getSkinningMatrices

// ----------------------------------------------------------------------------
s32 GESPM::getJointIDWithArm(const c8* name, unsigned* arm_id) const
{
    for (unsigned i = 0; i < m_all_armatures.size(); i++)
    {
        const Armature& arm = m_all_armatures[i];
        auto found = std::find(arm.m_joint_names.begin(),
            arm.m_joint_names.end(), name);
        if (found != arm.m_joint_names.end())
        {
            if (arm_id != NULL)
            {
                *arm_id = i;
            }
            return (int)(found - arm.m_joint_names.begin());
        }
    }
    return -1;
}   // getJointIDWithArm

// ----------------------------------------------------------------------------
void GESPM::removeMeshBuffer(u32 nr)
{
    if (nr < m_buffer.size())
    {
        m_buffer[nr]->drop();
        m_buffer.erase(m_buffer.begin() + nr);
    }
}   // removeMeshBuffer

}
