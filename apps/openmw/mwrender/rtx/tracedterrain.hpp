#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <osg/Array>
#include <osg/BoundingBox>
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

namespace Rtx
{
    class PredictedBands;
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
    /// word, which the ground that owns both forwards. Asked by a ray cast, and by nothing else; and where the game
    /// predicts the eye will stand, which the preload worker tells.
    class StandingGround
    {
    public:
        virtual bool standsGround(const osg::Vec2i& cell) const = 0;

        /// The placements the ring holds in `cell`, standing or not — `Rtx::CellStreamer::placementsIn`.
        virtual std::span<const Rtx::Placement> placementsIn(const osg::Vec2i& cell) const = 0;

        /// `Rtx::CellStreamer::getPredictedBands`, whose lock is its own, so any thread may tell it.
        virtual Rtx::PredictedBands& getPredictedBands() const = 0;

    protected:
        ~StandingGround() = default;
    };

    /// The ground as the ray tracer stands it: a `Terrain::World` that holds the storage, the worldspace and
    /// the active grid and draws no chunks, as `Rtx::CellStreamer` reads the land records itself. The preloader
    /// still asks every world for a view to fill, so upstream's callers run unchanged.
    ///
    /// Upstream's cell borders need no chunk: `ToggleBorders` stands a line strip over each loaded cell's
    /// edge under `Mask_Debug`, where `DebugWalk` reads it. They stand under a group of the terrain's mask so
    /// `tws` takes them with the ground, straight under the world root because the terrain root, which the
    /// scene root holds, is never entered by the walk.
    ///
    /// A grid per loaded cell serves the intersector, under `Mask_Terrain` and nothing else:
    /// `RenderingManager::castRay` walks the scene graph, so focus, dropping an object and Lua's
    /// `castRenderingRay` answer off the storage's heights under the rasterizer's own triangles, with no
    /// material, texture or normal. The mirror's walk leaves `Mask_Terrain` out, so the trace never meets one.
    ///
    /// Past the loaded cells, a cell the segment crosses whose ground `distance` says stands is filled into
    /// one scratch grid and met by the same intersector, nearest cell first, so a cast toward a hill four
    /// cells away meets the hill the trace draws. The statics the ring stands there are met in the
    /// template's own triangles at the part's place with its reference number, as a paged chunk's hit names
    /// it, under a node of `Mask_Static` so a cast that leaves statics out leaves these out.
    class TracedTerrain final : public Terrain::World
    {
    public:
        TracedTerrain(osg::Group& sceneRoot, osg::Group& worldRoot, Terrain::Storage& storage,
            Resource::SceneManager& scenes, const StandingGround& distance, std::uint32_t nodeMask,
            ESM::RefId worldspace);

        /// Out of line, where the grids' types are whole.
        ~TracedTerrain() override;

        /// **A view that is a predicted band's hold**: a slot of `Rtx::PredictedBands`, which `preload` fills with the
        /// position the preloader predicts and a reset or the view's going empties, upstream's own word that the
        /// position is predicted no more. Any thread.
        std::unique_ptr<Terrain::View> createView() override;

        /// Stands a band hidden around `viewPoint` from the next walk on, outdoors, and returns: the ring reads and
        /// stands the cells on the frame's share (`Rtx::CellStreamer`). On the preload worker.
        void preload(Terrain::View* view, const osg::Vec3f& viewPoint, const osg::Vec4i& cellgrid,
            std::atomic<bool>& abort, Loading::Reporter& reporter) override;

        /// Stands the cell's grid for the intersector out of one a cell that left gave back, the arrays
        /// refilled in place, so a cell arriving allocates nothing after the first few.
        void loadCell(std::int32_t x, std::int32_t y) override;

        /// Takes the cell's grid down and keeps it for the next cell to arrive.
        void unloadCell(std::int32_t x, std::int32_t y) override;

        /// Shows or hides the two answers and the borders, as `Terrain::QuadTreeWorld` hangs or takes down its
        /// root: `RenderingManager::enableTerrain` turns the last worldspace's ground off when another's comes
        /// on, and the distance and ring this one answers for are the last worldspace's.
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

        /// One part handed to the visitor: its place, the part, and the reference number a hit on it names.
        /// A pool, because every hit keeps the path it was met on until the caller reads it.
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
        const std::uint32_t mNodeMask;

        /// The worldspace's land in cells and a cell's side, read once: the land's box walks every land
        /// record, which every focus ray paid for, 0.13 ms a frame on a large content list.
        const osg::BoundingBoxd mLand;
        const double mCellSize;
    };
}
