#include "headersources.hpp"

#include <chrono>

#include <components/rtx/preprocess/headerderived.hpp>
#include <components/rtx/preprocess/imagederivation.hpp>

namespace MWRender
{
    namespace
    {
        std::uint32_t today()
        {
            return static_cast<std::uint32_t>(
                std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()).time_since_epoch().count());
        }
    }

    HeaderSources::HeaderSources(
        const VFS::Manager& vfs, Rtx::ImageDerivation& derivation, const std::optional<std::filesystem::path>& index)
        : mStamps(vfs)
        , mDerivation(derivation)
        , mDay(today())
    {
        if (index.has_value())
            mIndex.emplace(*index);
    }

    HeaderSources::~HeaderSources()
    {
        if (!mIndex.has_value())
            return;
        mIndex->dropUnnamed(mDay);
        std::ignore = mIndex->write();
    }

    Resource::HeaderLookup HeaderSources::find(const VFS::Path::NormalizedView path)
    {
        if (!mIndex.has_value())
            return Resource::HeaderLookup{};

        // **Stamped before the file is read**, so a file written between the stamp and the read is filed under the
        // stamp it had, which the next session's moves past.
        const Rtx::FileStamp stamp = mStamps.stampOf(path);
        const std::optional<Rtx::SourceRecord> record = mIndex->find(path.value(), stamp.mStamp, stamp.mModified, mDay);
        if (!record.has_value())
            return Resource::HeaderLookup{ .mFound = std::nullopt, .mStamp = stamp.mStamp };
        return Resource::HeaderLookup{ .mFound
            = Resource::HeaderFound{ .mShape = record->mShape, .mDerived = new Rtx::HeaderDerived(record->mFacts) },
            .mStamp = stamp.mStamp };
    }

    osg::ref_ptr<const osg::Referenced> HeaderSources::derive(const Read& read)
    {
        osg::ref_ptr<const Rtx::HeaderDerived> derived = mDerivation.derive(read.mTexels);
        if (mIndex.has_value())
            mIndex->file(read.mPath.value(),
                Rtx::SourceRecord{ .mStamp = read.mStamp, .mShape = read.mShape, .mFacts = derived->getFacts() }, mDay);
        return derived;
    }
}
