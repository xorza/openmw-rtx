#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>
#include <osg/Vec3ui>
#include <osg/Vec4f>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/shaders/scene.h>

#include "light.hpp"

namespace Rtx
{
    /// Which lamps can reach where: a uniform grid in world space, because a bounce lands where no
    /// pixel is looking, and a fog march asking every lamp twenty-four times a pixel was several
    /// times the trace. A lamp is binned into every cell its reach touches, so the shader's own
    /// distance test is a refinement and never a correction. The grid covers what the lamps reach
    /// and takes no bounds from the scene, so a fog step in the air above a cell is not handed an
    /// empty list.
    class LightGrid
    {
    public:
        /// An unfilled grid, for an owner that binds its lamps in a later step. Not the same as a
        /// grid built from no lamps, which has one empty cell: this has none, so `rebuild` has to
        /// run before anything looks a position up.
        LightGrid() = default;

        /// Bins `lights`, for a caller that has them at construction.
        explicit LightGrid(std::span<const Light> lights) { rebuild(lights); }

        /// Bins `lights` into the grid this already has, without going back to the allocator: a lamp
        /// whose box moved leaves the cells it left and enters the cells it entered, and nothing
        /// else is written — a lamp that only flickered has the same grid, and Morrowind's lamps
        /// flicker on nearly every frame. The grid is made again where a lamp came, went or reached
        /// out of it, or where the list has no room left for a run that grew.
        void rebuild(std::span<const Light> lights);

        /// The cells a sphere touches, as a half-open box of cell coordinates.
        struct CellBox
        {
            osg::Vec3ui mLow;
            osg::Vec3ui mHigh;

            std::size_t getCount() const
            {
                return std::size_t{ mHigh.x() - mLow.x() } * (mHigh.y() - mLow.y()) * (mHigh.z() - mLow.z());
            }

            bool contains(const osg::Vec3ui& at) const
            {
                return at.x() >= mLow.x() && at.x() < mHigh.x() && at.y() >= mLow.y() && at.y() < mHigh.y()
                    && at.z() >= mLow.z() && at.z() < mHigh.z();
            }

            bool operator==(const CellBox& other) const = default;
        };

        /// The corner cell zero starts at, and how many cells the grid is across.
        const osg::Vec3f& getOrigin() const { return mOrigin; }
        const osg::Vec3ui& getSize() const { return mSize; }

        /// One over the cell's side, which is what turns a position into a cell without a divide.
        float getInverseCell() const { return mInverseCell; }

        /// Every cell's two runs, cell `(z * size.y + y) * size.x + x`: the lamps that light it and
        /// those that take light away, each where it starts in `getList` and how many it holds. Each
        /// run in the order the lamps were given, which is the order a fill from nothing makes, so a
        /// lamp that moved leaves the runs a build would have made.
        ///
        /// **Each run in a block of its own room**, a power of two over what it held when it was
        /// placed, so a lamp that crosses a cell writes the cells it left and entered and nothing
        /// else, and the device's copies take only those (`getRewritten`). Runs packed end to end
        /// moved every run after the first one that changed, and a carried torch crosses a cell
        /// every few frames. A run that outgrows its block moves to one twice the size at the list's
        /// end, and a list with no room left there makes the grid again. A room of the densest
        /// cell's for every cell instead held the list to a budget only by coarsening the grid in a
        /// crowded exterior, where every lookup then walks more lamps.
        ///
        /// **The lamps that take light away are a run of their own**, so a walk of the lamps never
        /// meets one, and in the same row as the cell's own, so the two a shaded point reads are one
        /// fetch.
        std::span<const Shaders::GpuLightCell> getCells() const { return mCells; }

        /// The lamps the runs name, and room past them: what stands outside a run is never read.
        std::span<const std::uint32_t> getList() const { return mList; }

        /// Whether the last `rebuild` made the grid again, so that every cell and the list may have
        /// changed; where it did not, the cells whose runs it wrote, each once.
        bool wasMadeAgain() const { return mMadeAgain; }
        std::span<const Index> getRewritten() const { return mRewritten.getSlots(); }

        /// How many lights the grid bins, those that take light away among them.
        std::size_t getLightCount() const { return mBinnedOn.size(); }

    private:
        /// Makes the grid for `lights` from nothing: its extent, its cell, every lamp's box and every
        /// run's block.
        void build(std::span<const Light> lights);

        /// Whether `light`'s reach lies inside the grid as it stands, which is where its cells are.
        bool covers(const Light& light) const;

        /// Counts every run, lays its block and writes it, from `mBoxes`.
        void fill();

        /// Takes lamp `index` out of `cell`'s run under `key` — nought for the lamps that light and
        /// one for those that take light away — closing the run behind it.
        void leave(Index cell, std::uint32_t key, std::uint32_t index);

        /// Puts lamp `index` into `cell`'s run under `key`, in order, moving the run to a block twice
        /// the size where it is full. False, writing nothing, where the list has no room for that.
        bool enter(Index cell, std::uint32_t key, std::uint32_t index);

        osg::Vec3f mOrigin;
        osg::Vec3ui mSize{ 1u, 1u, 1u };
        float mInverseCell = 1.0f;

        std::vector<Shaders::GpuLightCell> mCells;
        std::vector<std::uint32_t> mList;

        /// How many lamps each run's block holds, two a cell as `mCells` keys them, and where the
        /// list's next block goes. Blocks are taken from the end and never given back: a build
        /// packs them again, and lamps come and go far more often than a list fills.
        std::vector<std::uint32_t> mRooms;
        std::uint32_t mEnd = 0;

        /// Where each light stood when it was last binned, and how far it reached — `xyz` and `w`,
        /// entry `i` for light `i`, the reach negated for a light that takes light away — and the
        /// cells that put it in. Refilled beside the list and never freed.
        std::vector<osg::Vec4f> mBinnedOn;
        std::vector<CellBox> mBoxes;

        bool mMadeAgain = false;
        SlotSet mRewritten;

        /// A lamp whose box moved this rebuild, and where it stood before: every lamp leaves before
        /// any enters, so two that trade places never crowd a run they are only passing through.
        struct Moved
        {
            std::uint32_t mIndex;
            CellBox mFrom;
            bool mTook;
        };

        /// Cleared and refilled by `rebuild`; never freed.
        std::vector<Moved> mMoved;
    };
}
