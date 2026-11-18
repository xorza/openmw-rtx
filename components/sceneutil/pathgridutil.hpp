#pragma once

#include <cstdint>

#include <osg/Geometry>
#include <osg/ref_ptr>

namespace ESM
{
    struct Pathgrid;
}

namespace SceneUtil
{
    constexpr float DiamondHalfHeight = 40.f;
    constexpr float DiamondHalfWidth = 16.f;

    osg::ref_ptr<osg::Geometry> createPathgridGeometry(const ESM::Pathgrid& pathgrid);

    osg::ref_ptr<osg::Geometry> createPathgridSelectedWireframe(
        const ESM::Pathgrid& pathgrid, const std::vector<std::uint16_t>& selected);

    std::uint16_t getPathgridNode(std::uint32_t vertexIndex);
}
