#pragma once

#include <vector>

#include <osg/Vec2i>

#include <components/esm/refid.hpp>
#include <components/terrain/pagedcellref.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// Where the world's groundcover is read from: the references the files `groundcover=` names
    /// place in each exterior cell, after the density rule, and the model each record names. An
    /// interface, as `ContentSource` is, so a ring can be handed grass by a test that has no files.
    /// The game answers out of `MWWorld::GroundcoverStore` (`MWRender::TracedGroundcover`).
    class GroundcoverSource
    {
    public:
        virtual ~GroundcoverSource() = default;

        /// Appends the references of the exterior cell at `cell` the density keeps, in the order of
        /// their numbers. Called on the ring's reader thread and nowhere else.
        virtual void collect(const osg::Vec2i& cell, std::vector<Terrain::PagedCellRef>& into) = 0;

        /// The model `record` names, empty where it names none. The reader thread's as well.
        virtual VFS::Path::NormalizedView modelOf(const ESM::RefId& record) const = 0;
    };
}
