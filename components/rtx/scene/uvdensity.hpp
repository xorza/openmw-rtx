#pragma once

#include <cstdint>
#include <span>

#include <osg/Vec2f>
#include <osg/Vec3f>

namespace Rtx
{
    /// **The least texture a mesh lays on a unit of its own surface**, per coordinate set: over its triangles, the
    /// smallest ratio of twice a triangle's area in the set's coordinates to twice its area in the mesh's own units.
    /// It is the `uvArea / cone.mArea` term of `coneBaseOf` (`texturing.glsl`) at its smallest, where the level a ray
    /// reads is finest. So half its log, with the cone's width and the texture's own resolution, bounds from below
    /// every level a ray reads off the mesh (`docs/architecture.md`, "A texture stands from the level its nearest user
    /// asks"). A triangle the shader reads at the finest level is nought here: one with no area in either space. So is
    /// every triangle of a set the mesh does not bring, since its coordinates then read nought everywhere. Infinite
    /// for a mesh of no triangles, which reads nothing, and nought until measured, so a mesh whose maker states none
    /// asks the finest level.
    struct UvDensity
    {
        float mFirst = 0.0f;
        float mSecond = 0.0f;

        bool operator==(const UvDensity&) const = default;
    };

    /// `positions`' triangles, `indices` three at a time, read through `first` and `second`, each empty where the mesh
    /// brings none of it or as long as `positions`.
    UvDensity uvDensityOf(std::span<const osg::Vec3f> positions, std::span<const osg::Vec2f> first,
        std::span<const osg::Vec2f> second, std::span<const std::uint32_t> indices);
}
