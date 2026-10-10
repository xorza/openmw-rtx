#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <osg/Array>
#include <osg/Group>
#include <osg/Vec2i>
#include <osg/ref_ptr>

#include <components/esm/refid.hpp>
#include <components/rtx/mirror/cells/held.hpp>
#include <components/terrain/buffercache.hpp>
#include <components/terrain/world.hpp>

namespace osg
{
    class Geometry;
    class MatrixTransform;
    class Node;
    class PositionAttitudeTransform;
}

namespace osgUtil
{
    class IntersectionVisitor;
    class LineSegmentIntersector;
}

namespace Resource
{
    class SceneManager;
}

namespace Terrain
{
    class Storage;
    class View;
}

namespace MWRender
{
    struct RefnumMarker;

    /// Which cells past the loaded ones stand ground the trace draws, and what statics: the ring's
    /// word, which the ground that owns both forwards. Asked by a ray cast, and by nothing else.
    class StandingGround
    {
    public:
        virtual bool standsGround(const osg::Vec2i& cell) const = 0;

        /// The placements the ring holds in `cell`, standing or not — `Rtx::CellRing::placementsIn`.
        virtual std::span<const Rtx::Placement> placementsIn(const osg::Vec2i& cell) const = 0;

    protected:
        ~StandingGround() = default;
    };

    /// The ground as the ray tracer stands it: a `Terrain::World` that holds the storage, the
    /// worldspace and the active grid, and draws no chunks — `Rtx::CellRing` reads the land
    /// records itself, and a chunk the game built beside it would be one nothing traces.
    ///
    /// **A world that says it has no chunks**, rather than one the game finds out about: the
    /// preloader asks every world for a view to fill, and is answered here, so upstream's callers
    /// run unchanged.
    ///
    /// **And upstream's cell borders**, which need no chunk: `ToggleBorders` stands a line strip
    /// over each loaded cell's edge from the storage's heights, under `Mask_Debug`, where
    /// `DebugWalk` reads the debug modes' lines. Under a group of the terrain's mask, as upstream
    /// stands them under its terrain root, so `tws` takes them with the ground; straight under the
    /// world root and not under the terrain root, which the scene root holds and the walk never
    /// enters.
    ///
    /// **And a grid per loaded cell for the intersector**, under `Mask_Terrain` and nothing else.
    /// `RenderingManager::castRay` walks the scene graph, so `terrain obstructs focus`, dropping
    /// an object on the ground and Lua's `castRenderingRay` all answer off whatever stands there:
    /// the storage's heights under the rasterizer's own triangles, with no material, no texture
    /// and no normal, which a ray asks nothing of. The mirror's walk leaves `Mask_Terrain` out, so
    /// the trace never meets one.
    ///
    /// **And past the loaded cells, the ground the ring stands**, which the rasterizer's quad tree
    /// answers with its chunks: a cell the segment crosses whose ground `distance` says stands is
    /// filled into one scratch grid of the same triangles and met by the same intersector, nearest
    /// cell first, so a cast toward a hill four cells away meets the hill the trace draws.
    ///
    /// **And the statics the ring stands there**, which the rasterizer's object paging answers with
    /// its merged chunks: each part the segment's cells hold is met in the template's own triangles
    /// at the part's place, with its reference number, as a paged chunk's hit names it. Under a node
    /// of `Mask_Static`, as the paging stands, so a cast that leaves statics out leaves these out.
    class TracedTerrain final : public Terrain::World
    {
    public:
        TracedTerrain(osg::Group& sceneRoot, osg::Group& worldRoot, Terrain::Storage& storage,
            Resource::SceneManager& scenes, const StandingGround& distance, unsigned int nodeMask,
            ESM::RefId worldspace);

        /// Out of line, where the grids' types are whole.
        ~TracedTerrain() override;

        /// A view that holds nothing and resets to nothing, because there is nothing to preload
        /// into it.
        std::unique_ptr<Terrain::View> createView() override;

        /// Stands the cell's grid for the intersector, out of a grid a cell that left gave back
        /// where there is one: the arrays are refilled in place, so a cell arriving while the game
        /// runs allocates nothing after the first few.
        void loadCell(int x, int y) override;

        /// Takes the cell's grid down and keeps it for the next cell to arrive.
        void unloadCell(int x, int y) override;

        /// Shows or hides the two answers and the borders, as `Terrain::QuadTreeWorld` hangs or
        /// takes down its root: `RenderingManager::enableTerrain` turns the last worldspace's ground
        /// off when another's comes on, and the distance and the ring this one answers for are the
        /// last worldspace's.
        void enable(bool enabled) override;

        /// What `segment`, `visitor`'s in this node's frame, meets of the distance's ground, inserted
        /// into its intersections as a loaded grid's would be. Called by the node this hangs under the
        /// terrain root, which the visitor enters whatever the segment.
        void meet(osgUtil::IntersectionVisitor& visitor, osgUtil::LineSegmentIntersector& segment);

        /// What `segment` meets of the statics the ring stands past the loaded cells, as `meet`
        /// answers for the ground. Called by the node this hangs under the scene root.
        void meetStatics(osgUtil::IntersectionVisitor& visitor, osgUtil::LineSegmentIntersector& segment);

    private:
        class StandingPart;

        /// One part handed to the visitor: its place, the part, and the reference number a hit on it
        /// names. A pool and not one, because every hit keeps the path it was met on until the
        /// caller reads it.
        struct Carrier
        {
            osg::ref_ptr<osg::MatrixTransform> mPlace;
            osg::ref_ptr<StandingPart> mPart;
            // Points into the user data `mPlace` carries, which owns the marker.
            RefnumMarker* mMarker = nullptr;
        };

        /// The next carrier of this cast, made where the pool has none spare.
        Carrier& nextCarrier();

        /// One cell's grid: which cell it stands, the transform at the cell's middle, and the
        /// geometry under it whose positions are the storage's.
        struct CellGrid
        {
            osg::Vec2i mCell{};
            osg::ref_ptr<osg::PositionAttitudeTransform> mRoot;
            osg::ref_ptr<osg::Geometry> mGeometry;
            osg::ref_ptr<osg::Vec3Array> mPositions;
        };

        /// A grid to stand a cell on: one a cell gave back, or a new one.
        CellGrid takeGrid();

        /// Fills `grid` with `cell`'s ground and stands it at the cell's middle.
        void fill(CellGrid& grid, const osg::Vec2i& cell);

        /// The grid standing `cell`, or null.
        CellGrid* findGrid(const osg::Vec2i& cell);

        /// The rasterizer's own triangles over a cell's vertices, shared by every grid.
        Terrain::BufferCache mBuffers;

        /// What the storage writes beside the positions and a ray never reads, refilled per cell.
        osg::ref_ptr<osg::Vec3Array> mNormals;
        osg::ref_ptr<osg::Vec4ubArray> mColours;

        /// The standing grids, searched by cell: a ring's worth, and a vector because a map allocated
        /// a node for every cell that arrived.
        std::vector<CellGrid> mCells;
        std::vector<CellGrid> mSpare;

        /// Borrowed: the ground that made this owns what answers.
        const StandingGround& mDistance;

        /// The one grid a distant cell is filled into for a ray, and which cell it holds.
        CellGrid mFar;
        std::optional<osg::Vec2i> mFarCell;

        /// The node under the terrain root that hands an intersection visitor to `meet`.
        osg::ref_ptr<osg::Node> mAnswer;

        /// The node under the scene root that hands one to `meetStatics`, of `Mask_Static`.
        osg::ref_ptr<osg::Node> mStaticsAnswer;

        /// The carriers, and how many this cast has used. Kept across casts, so a cast allocates
        /// nothing after the most hits one has met.
        std::vector<Carrier> mCarriers;
        std::size_t mCarried = 0;

        /// The cells this cast has asked for statics, so a cell beside two crossed ones is asked
        /// once.
        std::vector<osg::Vec2i> mAsked;

        /// The group the cell borders stand under, of the terrain's mask.
        osg::ref_ptr<osg::Group> mBorders;

        /// The terrain's mask, which `enable` gives the borders back.
        const unsigned int mNodeMask;
    };
}
