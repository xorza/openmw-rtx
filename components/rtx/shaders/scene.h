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

        /// The alpha below which a texel is a hole, or zero where the surface has none.
        ///
        /// The mode it came from does not survive the trip: what a cutout costs traversal is one
        /// comparison, and a material that wants none stores a threshold nothing can fail. Which
        /// instances stop to make that comparison at all is settled by the build, from the same
        /// number.
        float mAlphaCutoff;

        vec4 mDiffuseColour;
    };

#ifdef __cplusplus
}
#endif

#endif
