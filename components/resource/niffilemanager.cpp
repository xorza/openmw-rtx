#include "niffilemanager.hpp"

#include <cstdint>
#include <ios>

#include <components/files/istreamptr.hpp>
#include <components/files/utils.hpp>
#include <components/vfs/manager.hpp>

#include "objectcache.hpp"

namespace Resource
{

    NifFileManager::NifFileManager(
        const VFS::Manager* vfs, CacheBudget& budget, const ToUTF8::StatelessUtf8Encoder* encoder)
        : ResourceManager(vfs, budget)
        , mEncoder(encoder)
    {
    }

    NifFileManager::~NifFileManager() = default;

    Nif::NIFFilePtr NifFileManager::get(VFS::Path::NormalizedView name)
    {
        return mCache->getOrLoad(name.value(), [&] {
            auto file = std::make_shared<Nif::NIFFile>(name);
            Nif::Reader reader(*file, mEncoder);
            Files::IStreamPtr stream = mVFS->get(name);
            // What the file parses into is its records, near enough its own bytes, which nothing else counts.
            const std::streamsize bytes = Files::getStreamSizeLeft(*stream);
            reader.parse(std::move(stream));
            return Loaded<Nif::NIFFilePtr>{ .mValue = std::move(file), .mBytes = static_cast<std::uint64_t>(bytes) };
        });
    }

    void NifFileManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Nif", frameNumber, mCache->getStats(), *stats);
    }

}
