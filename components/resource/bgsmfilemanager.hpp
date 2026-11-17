#pragma once

#include <cstdint>

#include <components/bgsm/file.hpp>

#include "resourcemanager.hpp"

namespace Resource
{

    /// @brief Handles caching of material files.
    /// @note May be used from any thread.
    class BgsmFileManager : public ResourceManager<Bgsm::MaterialFilePtr>
    {
    public:
        BgsmFileManager(const VFS::Manager* vfs, double expiryDelay);
        ~BgsmFileManager() override = default;

        /// Retrieve a material file from the cache or load it from the VFS if not cached yet.
        Bgsm::MaterialFilePtr get(VFS::Path::NormalizedView name);

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;
    };

}
