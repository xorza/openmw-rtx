#pragma once

#include <map>
#include <memory>

#include "tileposition.hpp"

namespace DetourNavigator
{
    class RecastMesh;

    using RecastMeshTiles = std::map<TilePosition, std::shared_ptr<RecastMesh>>;
}
