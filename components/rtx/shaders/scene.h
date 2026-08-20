#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H

// The scene's tables, as both sides see them. Scalar block layout throughout, so a `uint` is four
// bytes and a `vec2` is eight on both sides and there is nothing to translate.

#ifdef __cplusplus

#include <cstdint>

#include <osg/Vec4f>

namespace Rtx::Shaders
{
    using vec4 = osg::Vec4f;
    using uint = std::uint32_t;

#endif

    /// A material with no texture in a slot stores this.
    const uint NO_TEXTURE = 0xFFFFFFFFu;

    const uint ALPHA_OPAQUE = 0u;
    const uint ALPHA_CUTOUT = 1u;
    const uint ALPHA_BLEND = 2u;

    /// Where a mesh's vertices and indices begin in the shared buffers.
    ///
    /// Indices are mesh-local, so a triangle's vertex is `mVertexOffset` plus what the index says.
    struct GpuMesh
    {
        uint mVertexOffset;
        uint mIndexOffset;
    };

    struct GpuInstance
    {
        uint mMesh;
        uint mMaterial;
    };

    struct GpuMaterial
    {
        uint mDiffuse;
        uint mAlphaMode;
        float mAlphaRef;
        vec4 mDiffuseColour;
    };

#ifdef __cplusplus
}
#endif

#endif
