#pragma once

#include <cstdint>

#include <osg/Object>

#include <components/esm/exteriorcelllocation.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/resource/resourcemanager.hpp>

namespace ESM
{
    struct Land;
}

namespace MWRender
{

    class LandManager : public Resource::GenericResourceManager<ESM::ExteriorCellLocation,
                            std::shared_ptr<const ESMTerrain::LandObject>>
    {
    public:
        explicit LandManager(std::int32_t loadFlags);

        /// @note Will return nullptr if not found.
        std::shared_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellIndex);

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;

    private:
        std::int32_t mLoadFlags;
    };

}
