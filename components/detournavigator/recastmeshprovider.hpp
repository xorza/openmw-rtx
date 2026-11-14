#pragma once

#include "tileposition.hpp"

#include <components/esm/refid.hpp>

#include <memory>

namespace DetourNavigator
{
    class RecastMesh;

    struct RecastMeshProvider
    {
        virtual ~RecastMeshProvider() = default;

        virtual std::shared_ptr<RecastMesh> getMesh(ESM::RefId worldspace, const TilePosition& tilePosition) const = 0;
    };
}
