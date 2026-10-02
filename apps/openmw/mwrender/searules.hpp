#pragma once

#include <osg/Geometry>
#include <osg/Vec2f>
#include <osg/ref_ptr>

#include <components/misc/constants.hpp>
#include <components/sceneutil/waterutil.hpp>

namespace MWRender
{
    /// The sea's plane, as the game lays it under an exterior: a hundred and fifty cells across, in
    /// forty segments, its texture repeating every nine hundred units. The rasterizer's water and the
    /// ray tracer's sea are both this plane.
    inline osg::ref_ptr<osg::Geometry> createSeaGeometry()
    {
        return SceneUtil::createWaterGeometry(Constants::CellSizeInUnits * 150, 40, 900);
    }

    /// Where the sea is centred under the exterior cell at `gridX`, `gridY`: the cell's middle.
    /// Indoors it stands at the origin.
    inline osg::Vec2f seaCentre(int gridX, int gridY)
    {
        constexpr int half = Constants::CellSizeInUnits / 2;
        return osg::Vec2f(static_cast<float>(gridX * Constants::CellSizeInUnits + half),
            static_cast<float>(gridY * Constants::CellSizeInUnits + half));
    }
}
