#include "ge_spm_buffer.hpp"

#include "ge_main.hpp"
#include "ge_vulkan_driver.hpp"
#include "ge_vulkan_features.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "mini_glm.hpp"

namespace GE
{
// ----------------------------------------------------------------------------
void GESPMBuffer::createVertexIndexBuffer()
{
    if (GEVulkanFeatures::supportsBaseVertexRendering())
        return;

    GEVulkanDriver* vk = getVKDriver();
    size_t total_pitch = getVertexPitchFromType(video::EVT_SKINNED_MESH);
    size_t bone_pitch = sizeof(int16_t) * 8;
    size_t static_pitch = total_pitch - bone_pitch;
    size_t vbo_size = getVertexCount() * static_pitch;
    m_ibo_offset = vbo_size;
    size_t ibo_size = getIndexCount() * sizeof(uint16_t);
    size_t total_size = vbo_size + ibo_size;
    if (m_has_skinning)
    {
        total_size += getPadding(total_size, 4);
        m_skinning_vbo_offset = total_size;
        total_size += getVertexCount() * bone_pitch;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_memory = VK_NULL_HANDLE;
    VmaAllocationCreateInfo staging_buffer_create_info = {};
    staging_buffer_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    staging_buffer_create_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    staging_buffer_create_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!vk->createBuffer(total_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buffer_create_info,
        staging_buffer, staging_memory))
    {
        throw std::runtime_error("createVertexIndexBuffer create staging "
            "buffer failed");
    }

    uint8_t* mapped;
    if (vmaMapMemory(vk->getVmaAllocator(), staging_memory,
        (void**)&mapped) != VK_SUCCESS)
        throw std::runtime_error("createVertexIndexBuffer vmaMapMemory failed");

    size_t real_size = getVertexCount() * total_pitch;
    copyToMappedBuffer((uint32_t*)mapped, this);
    uint8_t* loc = mapped + getVertexCount() * static_pitch;
    memcpy(loc, m_indices.data(), m_indices.size() * sizeof(uint16_t));

    if (m_has_skinning)
    {
        loc = mapped + m_skinning_vbo_offset;
        for (unsigned i = 0; i < real_size; i += total_pitch)
        {
            uint8_t* vertices = ((uint8_t*)getVertices()) + i +
                static_pitch;
            memcpy(loc, vertices, bone_pitch);
            loc += bone_pitch;
        }
    }

    vmaUnmapMemory(vk->getVmaAllocator(), staging_memory);
    vmaFlushAllocation(vk->getVmaAllocator(), staging_memory, 0, total_size);

    VmaAllocationCreateInfo local_create_info = {};
    local_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk->createBuffer(total_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, local_create_info, m_buffer,
        m_memory))
        throw std::runtime_error("updateCache create buffer failed");

    vk->copyBuffer(staging_buffer, m_buffer, total_size);
    vmaDestroyBuffer(vk->getVmaAllocator(), staging_buffer, staging_memory);
}   // createVertexIndexBuffer

// ----------------------------------------------------------------------------
void GESPMBuffer::destroyVertexIndexBuffer()
{
    if (m_buffer == VK_NULL_HANDLE || m_memory == VK_NULL_HANDLE)
        return;

    getVKDriver()->waitIdle();
    vmaDestroyBuffer(getVKDriver()->getVmaAllocator(), m_buffer, m_memory);
    m_buffer = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
}   // destroyVertexIndexBuffer

// ----------------------------------------------------------------------------
void GESPMBuffer::setNormal(u32 i, const core::vector3df& normal)
{
    m_vertices[i].m_normal = MiniGLM::compressVector3(normal);
}   // setNormal

// ----------------------------------------------------------------------------
void GESPMBuffer::setTCoords(u32 i, const core::vector2df& tcoords)
{
    m_vertices[i].m_all_uvs[0] = MiniGLM::toFloat16(tcoords.X);
    m_vertices[i].m_all_uvs[1] = MiniGLM::toFloat16(tcoords.Y);
}   // setTCoords

// ----------------------------------------------------------------------------
namespace
{
// Linear midpoint of two skinned-mesh vertices, decoding/re-encoding the packed
// attributes (normal/tangent 10-10-10-2, UVs half-float) so interpolation is
// done in the real domain.
video::S3DVertexSkinnedMesh midpointVertex(
    const video::S3DVertexSkinnedMesh& a,
    const video::S3DVertexSkinnedMesh& b)
{
    video::S3DVertexSkinnedMesh m;
    m.m_position = (a.m_position + b.m_position) * 0.5f;

    core::vector3df n = MiniGLM::decompressVector3(a.m_normal) +
                        MiniGLM::decompressVector3(b.m_normal);
    if (n.getLengthSQ() > 1e-10f) n.normalize(); else n = MiniGLM::decompressVector3(a.m_normal);
    m.m_normal = MiniGLM::compressVector3(n);

    core::vector3df t = MiniGLM::decompressVector3(a.m_tangent) +
                        MiniGLM::decompressVector3(b.m_tangent);
    if (t.getLengthSQ() > 1e-10f) t.normalize(); else t = MiniGLM::decompressVector3(a.m_tangent);
    // Preserve the full 2-bit bitangent-sign field (bits 30-31): a mirrored-UV
    // surface encodes w = -1 as 0b10/0b11, so masking only bit 30 would flip it
    // to +1 and invert normal-mapped lighting on the subdivided vertices.
    // compressVector3 leaves the top 2 bits zero, so OR-ing vertex a's field is
    // safe (handedness is consistent across a well-formed surface).
    m.m_tangent = MiniGLM::compressVector3(t) | (a.m_tangent & (3u << 30));

    m.m_color = a.m_color.getInterpolated(b.m_color, 0.5f);
    for (int i = 0; i < 4; i++)
    {
        m.m_all_uvs[i] = MiniGLM::toFloat16(0.5f *
            (MiniGLM::toFloat32(a.m_all_uvs[i]) +
             MiniGLM::toFloat32(b.m_all_uvs[i])));
    }
    // Static (non-skinned) geometry: joint indices/weights are unused.
    return m;
}   // midpointVertex
}   // namespace

// ----------------------------------------------------------------------------
void GESPMBuffer::subdivideForRelativity()
{
    if (m_has_skinning || m_indices.size() < 3)
        return;

    // Subdivide until the LONGEST triangle edge is this small (world units) so
    // the nonlinear warp reads as a smooth curve rather than faceted. This is a
    // fixed absolute target shared by every buffer, so an edge shared by two
    // buffers gets the same number of halvings in both and their midpoints
    // coincide (no seam cracks). It also means a mesh of mixed tiny+huge
    // triangles is still refined (its longest edge drives the loop), unlike an
    // average-edge test that such a mesh would slip past.
    const float TARGET_EDGE = 0.6f;

    // Longest triangle edge over the whole buffer (true max, not sampled, so
    // the stop criterion is exact and seam-consistent).
    auto maxEdge = [this]() -> float
    {
        float longest = 0.0f;
        for (size_t t = 0; t + 2 < m_indices.size(); t += 3)
        {
            const core::vector3df& a = m_vertices[m_indices[t + 0]].m_position;
            const core::vector3df& b = m_vertices[m_indices[t + 1]].m_position;
            const core::vector3df& c = m_vertices[m_indices[t + 2]].m_position;
            longest = std::max(longest, a.getDistanceFrom(b));
            longest = std::max(longest, b.getDistanceFrom(c));
            longest = std::max(longest, c.getDistanceFrom(a));
        }
        return longest;
    };

    if (maxEdge() <= TARGET_EDGE)
        return;   // already fine for the per-vertex warp

    // A mesh buffer indexes vertices with 16-bit indices (max 65535). One pass
    // inserts one vertex per UNIQUE edge, and there are at most m_indices.size()
    // edge candidates (3 per triangle), so bound the pre-pass vertex count by
    // that worst case - correct even for triangle-soup / low-reuse geometry,
    // unlike assuming manifold ~4x growth. The (uint16_t) cast below then can
    // never truncate.
    const size_t INDEX_LIMIT = 65000;
    // Hard cap on passes as a final backstop against runaway growth.
    for (int pass = 0; pass < 8; pass++)
    {
        if (maxEdge() <= TARGET_EDGE)
            break;
        if (m_vertices.size() + m_indices.size() > INDEX_LIMIT)
            break;   // a further pass could exceed the 16-bit index range

        // One uniform midpoint subdivision (1 triangle -> 4). A shared edge
        // cache guarantees adjacent triangles use the same midpoint vertex, so
        // there are no cracks or T-junctions within this buffer.
        std::unordered_map<uint32_t, uint16_t> edge_mid;
        edge_mid.reserve(m_indices.size());
        std::vector<uint16_t> new_indices;
        new_indices.reserve(m_indices.size() * 4);
        bool overflow = false;
        auto getMid = [&](uint16_t v0, uint16_t v1) -> uint16_t
        {
            uint32_t key = v0 < v1 ? ((uint32_t)v0 << 16) | v1
                                   : ((uint32_t)v1 << 16) | v0;
            auto it = edge_mid.find(key);
            if (it != edge_mid.end())
                return it->second;
            if (m_vertices.size() >= 65535)   // never truncate the u16 index
            {
                overflow = true;
                return v0;
            }
            uint16_t ni = (uint16_t)m_vertices.size();
            m_vertices.push_back(midpointVertex(m_vertices[v0], m_vertices[v1]));
            edge_mid[key] = ni;
            return ni;
        };
        for (size_t t = 0; t + 2 < m_indices.size(); t += 3)
        {
            uint16_t a = m_indices[t], b = m_indices[t + 1], c = m_indices[t + 2];
            uint16_t ab = getMid(a, b), bc = getMid(b, c), ca = getMid(c, a);
            const uint16_t sub[12] =
                { a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca };
            new_indices.insert(new_indices.end(), sub, sub + 12);
        }
        if (overflow)
            break;   // leave this buffer at its current (valid) subdivision
        m_indices.swap(new_indices);
    }
    recalculateBoundingBox();
}   // subdivideForRelativity

}
