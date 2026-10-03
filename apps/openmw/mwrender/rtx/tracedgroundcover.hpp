#pragma once

#include <vector>

#include <boost/container/flat_map.hpp>

#include <osg/Vec2i>

#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/refnum.hpp>
#include <components/rtx/mirror/cells/groundcoversource.hpp>
#include <components/terrain/pagedcellref.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWWorld
{
    class GroundcoverStore;
}

namespace MWRender
{
    /// The world's groundcover as the ray tracer's ring reads it: the references the groundcover
    /// files place in an exterior cell, kept by the rasterizer's density rule and reduced over the
    /// files as its chunk reduces them, and the model `GroundcoverStore` names
    /// for each record.
    ///
    /// **Read per cell where the rasterizer reads per chunk.** Its chunk resets the density at
    /// every cell and counts a reference before it asks the chunk's border, so a cell keeps the
    /// same plants under any chunk that covers it, and this keeps those.
    class TracedGroundcover final : public Rtx::GroundcoverSource
    {
    public:
        /// @param density `[Groundcover] density`.
        /// @param lampLit `[Groundcover] point lighting`.
        TracedGroundcover(const MWWorld::GroundcoverStore& store, float density, bool lampLit);

        void collect(const osg::Vec2i& cell, std::vector<Terrain::PagedCellRef>& into) override;

        VFS::Path::NormalizedView modelOf(const ESM::RefId& record) const override;

        bool lampLit() const override { return mLampLit; }

    private:
        const MWWorld::GroundcoverStore& mStore;
        float mDensity;
        bool mLampLit;

        /// The ring's reader thread's own, which is the one thread `collect` is called on: the
        /// files stay open from one cell to the next.
        ESM::ReadersCache mReaders;

        // Refilled per cell.
        ESM::Cell mCell;
        ESM::CellRef mRef;

        /// What the files said last of each reference of the cell, in the order of their numbers,
        /// which is the order the rasterizer's map hands them on in. Refilled per cell.
        boost::container::flat_map<ESM::RefNum, Terrain::PagedCellRef> mKept;
    };
}
