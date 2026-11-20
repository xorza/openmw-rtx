#pragma once

#include <cstdint>

#include <osg/Vec3f>

namespace MWRender
{
    /// What changed the cells the game holds, which says what a renderer waits on before it shows the world again
    /// (`Renderer::standWhole`).
    enum class WorldChange : std::uint8_t
    {
        /// A walk across a cell's edge: the grid moved under an eye that did not jump.
        Crossing,

        /// A load, a teleport, a door or a fast travel: the eye stands somewhere new.
        Cut,
    };

    /// A change of the cells, and the position the eye is to stand at, which the player is moved to only after.
    struct WorldChanged
    {
        WorldChange mChange = WorldChange::Cut;
        osg::Vec3f mPosition;
    };
}
