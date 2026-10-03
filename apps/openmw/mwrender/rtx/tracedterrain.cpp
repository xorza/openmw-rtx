#include "tracedterrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>

#include <osg/BoundingSphere>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/PositionAttitudeTransform>
#include <osg/UserDataContainer>
#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <components/terrain/cellborder.hpp>
#include <components/terrain/storage.hpp>
#include <components/terrain/view.hpp>

#include "../objectpaging.hpp"
#include "../vismask.hpp"

namespace MWRender
{
    namespace
    {
        class NullView final : public Terrain::View
        {
        public:
            void reset() override {}
        };

        /// Hands an intersection visitor to one of `TracedTerrain`'s answers and answers every other
        /// visitor with nothing.
        ///
        /// **Culling off and no bound**, so an intersection visitor enters it and every node above
        /// it whatever their bounds say — OpenSceneGraph turns culling off up the chain for a child
        /// that has it off — and the scene's bound stays what is drawn under it. Bound by the land,
        /// as upstream's quad tree is, the scene's sphere would take the whole island in, and the
        /// traced map, which stands its eye at the top of that sphere, would meet its ground from
        /// 200,000 units up, where a hit's position has a sixty-fourth of a unit to say where a
        /// road's edge is.
        class DistantAnswer final : public osg::Node
        {
        public:
            using Answer = void (TracedTerrain::*)(osgUtil::IntersectionVisitor&);

            DistantAnswer(TracedTerrain& terrain, Answer answer)
                : mTerrain(terrain)
                , mAnswer(answer)
            {
                setCullingActive(false);
            }

            void traverse(osg::NodeVisitor& visitor) override
            {
                if (visitor.getVisitorType() == osg::NodeVisitor::INTERSECTION_VISITOR)
                    (mTerrain.*mAnswer)(static_cast<osgUtil::IntersectionVisitor&>(visitor));
            }

        private:
            TracedTerrain& mTerrain;
            Answer mAnswer;
        };

        /// The part of the segment from `from` to `to` over the box from `low` to `high`, as the
        /// two parameters along it, or nothing where it misses: the slab rule on two axes.
        std::optional<osg::Vec2d> clipToBox(
            const osg::Vec2d& from, const osg::Vec2d& to, const osg::Vec2d& low, const osg::Vec2d& high)
        {
            double enter = 0.0;
            double leave = 1.0;
            for (int axis = 0; axis < 2; ++axis)
            {
                const double along = to[axis] - from[axis];
                if (along == 0.0)
                {
                    if (from[axis] < low[axis] || from[axis] > high[axis])
                        return std::nullopt;
                    continue;
                }

                const double a = (low[axis] - from[axis]) / along;
                const double b = (high[axis] - from[axis]) / along;
                enter = std::max(enter, std::min(a, b));
                leave = std::min(leave, std::max(a, b));
            }

            if (enter > leave)
                return std::nullopt;
            return osg::Vec2d(enter, leave);
        }

        /// Visits the cells the segment from `from` to `to`, in cells, crosses over the box from
        /// `low` to `high`, in the order it crosses them, until `visit` answers false: Amanatides and
        /// Woo's walk, where each step takes the nearer of the next column and the next row. Exactly
        /// as many cells as the two ends are apart in columns and rows, plus the first.
        template <class Visit>
        void walkCells(
            const osg::Vec2d& from, const osg::Vec2d& to, const osg::Vec2d& low, const osg::Vec2d& high, Visit visit)
        {
            const std::optional<osg::Vec2d> over = clipToBox(from, to, low, high);
            if (!over.has_value())
                return;

            const osg::Vec2d d = to - from;
            const osg::Vec2d first = from + d * over->x();
            const osg::Vec2d last = from + d * over->y();
            osg::Vec2i cell(static_cast<int>(std::floor(first.x())), static_cast<int>(std::floor(first.y())));
            const osg::Vec2i end(static_cast<int>(std::floor(last.x())), static_cast<int>(std::floor(last.y())));
            const osg::Vec2i step(d.x() < 0.0 ? -1 : 1, d.y() < 0.0 ? -1 : 1);
            constexpr double never = std::numeric_limits<double>::infinity();
            const auto firstCrossing = [&](const int axis) {
                if (d[axis] == 0.0)
                    return never;
                const double boundary = step[axis] > 0 ? cell[axis] + 1.0 : static_cast<double>(cell[axis]);
                return (boundary - from[axis]) / d[axis];
            };
            osg::Vec2d crossing(firstCrossing(0), firstCrossing(1));
            const osg::Vec2d across(
                d.x() == 0.0 ? never : 1.0 / std::abs(d.x()), d.y() == 0.0 ? never : 1.0 / std::abs(d.y()));

            const int cells = std::abs(end.x() - cell.x()) + std::abs(end.y() - cell.y()) + 1;
            for (int crossed = 0; crossed < cells; ++crossed)
            {
                if (!visit(cell))
                    return;

                const int axis = crossing.x() < crossing.y() ? 0 : 1;
                cell[axis] += step[axis];
                crossing[axis] += across[axis];
            }
        }
    }

    TracedTerrain::TracedTerrain(osg::Group& sceneRoot, osg::Group& worldRoot, Terrain::Storage& storage,
        Resource::SceneManager& scenes, const StandingGround& distance, const unsigned int nodeMask,
        const ESM::RefId worldspace)
        : Terrain::World(&sceneRoot, &storage, nodeMask, worldspace)
        , mNormals(new osg::Vec3Array)
        , mColours(new osg::Vec4ubArray)
        , mDistance(distance)
        , mBorders(new osg::Group)
    {
        mBorders->setNodeMask(nodeMask);
        worldRoot.addChild(mBorders);
        mCellBorder = std::make_unique<Terrain::CellBorder>(this, mBorders.get(), Mask_Debug, &scenes);
        mFar = takeGrid();
        mAnswer = new DistantAnswer(*this, &TracedTerrain::meet);
        mTerrainRoot->addChild(mAnswer);
        mStaticsAnswer = new DistantAnswer(*this, &TracedTerrain::meetStatics);
        mStaticsAnswer->setNodeMask(Mask_Static);
        sceneRoot.addChild(mStaticsAnswer);
    }

    TracedTerrain::~TracedTerrain()
    {
        for (const osg::ref_ptr<osg::Node>& hung : { osg::ref_ptr<osg::Node>(mBorders), mStaticsAnswer })
            while (hung->getNumParents() > 0)
                hung->getParent(0)->removeChild(hung);
    }

    Terrain::View* TracedTerrain::createView()
    {
        return new NullView;
    }

    TracedTerrain::CellGrid TracedTerrain::takeGrid()
    {
        if (!mSpare.empty())
        {
            CellGrid grid = std::move(mSpare.back());
            mSpare.pop_back();
            return grid;
        }

        CellGrid grid{
            .mRoot = new osg::PositionAttitudeTransform,
            .mGeometry = new osg::Geometry,
            .mPositions = new osg::Vec3Array,
        };

        // The triangles `TerrainGrid` draws a cell with, and no stitching flags: the vertex order
        // is `fillVertexBuffers`' own, and the diamond splits each quad the way the game does, so
        // a slope is cut here as it is cut on the screen.
        const auto verts = static_cast<unsigned int>(mStorage->getCellVertices(mWorldspace));
        grid.mGeometry->setUseDisplayList(false);
        grid.mGeometry->setVertexArray(grid.mPositions);
        grid.mGeometry->addPrimitiveSet(mBuffers.getIndexBuffer(verts, 0));
        grid.mRoot->addChild(grid.mGeometry);

        return grid;
    }

    TracedTerrain::CellGrid* TracedTerrain::findGrid(const osg::Vec2i& cell)
    {
        const auto found
            = std::find_if(mCells.begin(), mCells.end(), [&](const CellGrid& grid) { return grid.mCell == cell; });
        return found == mCells.end() ? nullptr : &*found;
    }

    void TracedTerrain::fill(CellGrid& grid, const osg::Vec2i& cell)
    {
        // The whole cell at full detail, as the ring reads it: positions about the cell's middle,
        // which is where the transform stands them. A cell with no land record is the default
        // plane, which is the ground the rasterizer's chunk stands there too.
        grid.mCell = cell;
        const osg::Vec2f centre(static_cast<float>(cell.x()) + 0.5f, static_cast<float>(cell.y()) + 0.5f);
        mStorage->fillVertexBuffers(0, 1.0f, centre, mWorldspace, *grid.mPositions, *mNormals, *mColours);
        grid.mPositions->dirty();
        grid.mGeometry->dirtyBound();

        const float cellSize = mStorage->getCellWorldSize(mWorldspace);
        grid.mRoot->setPosition(osg::Vec3f(centre.x() * cellSize, centre.y() * cellSize, 0.0f));
    }

    void TracedTerrain::loadCell(const int x, const int y)
    {
        const osg::Vec2i cell(x, y);
        if (findGrid(cell) != nullptr)
            return;

        Terrain::World::loadCell(x, y);

        CellGrid grid = takeGrid();
        fill(grid, cell);
        mTerrainRoot->addChild(grid.mRoot);
        mCells.push_back(std::move(grid));
    }

    void TracedTerrain::unloadCell(const int x, const int y)
    {
        CellGrid* const found = findGrid(osg::Vec2i(x, y));
        if (found == nullptr)
            return;

        Terrain::World::unloadCell(x, y);

        mTerrainRoot->removeChild(found->mRoot);
        mSpare.push_back(std::move(*found));
        if (found != &mCells.back())
            *found = std::move(mCells.back());
        mCells.pop_back();
    }

    /// A part of a static the ring stands, as the intersection visitor meets it: the template's
    /// drawable, handed the visitor in the frame the carrier above pushed, and bound by the
    /// drawable's own sphere so the carrier culls the segment first. **Read and never changed**: the
    /// drawable is every reference's of the model, and the reader's threads read it too, so it is
    /// put under no node of this.
    class TracedTerrain::StandingPart final : public osg::Node
    {
    public:
        void show(const osg::Drawable& drawable)
        {
            mDrawable = &drawable;
            dirtyBound();
        }

        osg::BoundingSphere computeBound() const override
        {
            return mDrawable != nullptr ? mDrawable->getBound() : osg::BoundingSphere();
        }

        void traverse(osg::NodeVisitor& visitor) override
        {
            if (mDrawable != nullptr)
                const_cast<osg::Drawable&>(*mDrawable).accept(visitor);
        }

    private:
        const osg::Drawable* mDrawable = nullptr;
    };

    TracedTerrain::Carrier& TracedTerrain::nextCarrier()
    {
        if (mCarried == mCarriers.size())
        {
            Carrier made{ .mPlace = new osg::MatrixTransform, .mPart = new StandingPart, .mMarker = new RefnumMarker };
            made.mPlace->addChild(made.mPart);
            made.mPlace->getOrCreateUserDataContainer()->addUserObject(made.mMarker);
            mCarriers.push_back(std::move(made));
        }
        return mCarriers[mCarried++];
    }

    void TracedTerrain::meet(osgUtil::IntersectionVisitor& visitor)
    {
        osgUtil::Intersector* const asked = visitor.getIntersector();
        if (asked == nullptr)
            return;

        // In this node's frame, which the visitor clones its intersector into for every frame it
        // enters: a ray cast from the camera starts in its projection.
        const osg::ref_ptr<osgUtil::Intersector> here = asked->clone(visitor);
        auto* const segment = dynamic_cast<osgUtil::LineSegmentIntersector*>(here.get());
        if (segment == nullptr)
            return;

        // The segment on the plane in cells, clipped to the land the storage has, so a segment
        // of any length crosses no more cells than the land holds.
        const double cellSize = mStorage->getCellWorldSize(mWorldspace);
        const osg::Vec2d from(segment->getStart().x() / cellSize, segment->getStart().y() / cellSize);
        const osg::Vec2d to(segment->getEnd().x() / cellSize, segment->getEnd().y() / cellSize);
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        mStorage->getBounds(minX, maxX, minY, maxY, mWorldspace);

        const bool nearestOnly = segment->getIntersectionLimit() != osgUtil::Intersector::NO_LIMIT;
        walkCells(from, to, osg::Vec2d(minX, minY), osg::Vec2d(maxX, maxY), [&](const osg::Vec2i& cell) {
            if (findGrid(cell) != nullptr || !mDistance.standsGround(cell))
                return true;

            if (mFarCell != cell)
            {
                fill(mFar, cell);
                mFarCell = cell;
            }

            // A hit in a nearer cell is nearer than any in a cell after it.
            const std::size_t before = segment->getIntersections().size();
            mFar.mRoot->accept(visitor);
            return !nearestOnly || segment->getIntersections().size() == before;
        });
    }

    void TracedTerrain::meetStatics(osgUtil::IntersectionVisitor& visitor)
    {
        osgUtil::Intersector* const asked = visitor.getIntersector();
        if (asked == nullptr)
            return;

        const osg::ref_ptr<osgUtil::Intersector> here = asked->clone(visitor);
        auto* const segment = dynamic_cast<osgUtil::LineSegmentIntersector*>(here.get());
        if (segment == nullptr)
            return;

        // **Every crossed cell and the eight beside it, and none cut short**: a static stands in the
        // cell of its reference and its parts reach into the next, so a part a nearer cell's ground
        // hides is not nearer than a tower whose reference stands a cell on. Over the land the
        // storage has and a cell past it, where a reference stands in the sea.
        const double cellSize = mStorage->getCellWorldSize(mWorldspace);
        const osg::Vec2d from(segment->getStart().x() / cellSize, segment->getStart().y() / cellSize);
        const osg::Vec2d to(segment->getEnd().x() / cellSize, segment->getEnd().y() / cellSize);
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        mStorage->getBounds(minX, maxX, minY, maxY, mWorldspace);

        mCarried = 0;
        mAsked.clear();
        walkCells(from, to, osg::Vec2d(double{ minX } - 1.0, double{ minY } - 1.0),
            osg::Vec2d(double{ maxX } + 1.0, double{ maxY } + 1.0), [&](const osg::Vec2i& crossed) {
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const osg::Vec2i cell(crossed.x() + dx, crossed.y() + dy);
                        // A loaded cell's statics are the game's own nodes, which the visitor meets.
                        if (findGrid(cell) != nullptr || std::ranges::find(mAsked, cell) != mAsked.end())
                            continue;
                        mAsked.push_back(cell);

                        for (const Rtx::Placement& placement : mDistance.placementsIn(cell))
                        {
                            if (!placement.mStood.isStanding() || placement.mDrawable == nullptr)
                                continue;

                            Carrier& carrier = nextCarrier();
                            carrier.mPlace->setMatrix(placement.mStood.mTransform);
                            carrier.mPart->show(*placement.mDrawable);
                            carrier.mMarker->mRefnum = placement.mState.mRefNum;
                            carrier.mPlace->accept(visitor);
                        }
                    }
                return true;
            });
    }
}
