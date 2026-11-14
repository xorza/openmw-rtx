#pragma once

#include "tileposition.hpp"

#include <map>
#include <memory>

namespace DetourNavigator
{
    class RecastMesh;

    using RecastMeshTiles = std::map<TilePosition, std::shared_ptr<RecastMesh>>;
}
