#pragma once

#include <cstdint>

#include <components/spt/file.hpp>

#include "resourcemanager.hpp"

namespace Resource
{
    class SptFileManager : public ResourceManager<Spt::SPTFilePtr>
    {
    public:
        SptFileManager(const VFS::Manager* vfs, double expiryDelay);
        ~SptFileManager() override = default;

        Spt::SPTFilePtr get(VFS::Path::NormalizedView name);

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;
    };
}
