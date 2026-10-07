#include "tracedgroundcover.hpp"

#include <cstddef>
#include <exception>

#include <apps/openmw/mwrender/groundcoverdensity.hpp>
#include <apps/openmw/mwworld/groundcoverstore.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/esmreader.hpp>

namespace MWRender
{
    TracedGroundcover::TracedGroundcover(
        const MWWorld::GroundcoverStore& store, const float density, const bool lampLit)
        : mStore(store)
        , mDensity(density)
        , mLampLit(lampLit)
    {
    }

    void TracedGroundcover::collect(const osg::Vec2i& cell, std::vector<Terrain::PagedCellRef>& into)
    {
        if (mDensity <= 0.0f)
            return;

        // `Cell::blank` leaves the list of files alone, and the store assigns one only for a cell
        // a file lists: a cell no file lists read after one that some do read the files of the
        // last.
        mCell.mContextList.clear();
        mStore.initCell(mCell, cell.x(), cell.y());
        mKept.clear();

        DensityCalculator density(mDensity);
        for (std::size_t at = 0; at < mCell.mContextList.size(); ++at)
        {
            // A file this cannot read is the files' to answer for, and the cell keeps what the
            // others said, as the paging's walk of the same files keeps it.
            try
            {
                const ESM::ReadersCache::BusyItem reader
                    = mReaders.get(static_cast<std::size_t>(mCell.mContextList[at].index));
                mCell.restore(*reader, at);

                bool deleted = false;
                while (ESM::Cell::getNextRef(*reader, mRef, deleted))
                {
                    const auto said = mKept.lower_bound(mRef.mRefNum);
                    const bool known = said != mKept.end() && said->first == mRef.mRefNum;

                    // Counted only where the reference is not kept already: a later file that moves
                    // a plant does not take another's turn.
                    if (deleted || (!known && !density.isInstanceEnabled()))
                    {
                        if (known)
                            mKept.erase(said);
                        continue;
                    }

                    const Terrain::PagedCellRef ref{
                        .mRefId = mRef.mRefID,
                        .mRefNum = mRef.mRefNum,
                        .mPosition = mRef.mPos.asVec3(),
                        .mRotation = mRef.mPos.asRotationVec3(),
                        .mScale = mRef.mScale,
                    };
                    if (known)
                        said->second = ref;
                    else
                        mKept.emplace_hint(said, mRef.mRefNum, ref);
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to collect the groundcover of the cell (" << cell.x() << ", " << cell.y()
                                    << "): " << e.what();
            }
        }

        for (const auto& [refNum, ref] : mKept)
            into.push_back(ref);
    }

    VFS::Path::NormalizedView TracedGroundcover::modelOf(const ESM::RefId& record) const
    {
        return mStore.getGroundcoverModel(record);
    }
}
