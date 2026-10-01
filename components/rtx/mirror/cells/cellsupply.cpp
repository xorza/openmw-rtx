#include "cellsupply.hpp"

#include <components/crashcatcher/crashnote.hpp>

#include "cellreader.hpp"
#include "prepared.hpp"

namespace Rtx
{
    void CellRequest::take(CellRequest& from)
    {
        // Swapped rather than copied: what this hands back is the room the last request grew, which
        // the next one to reach the frame's side refills.
        mCells.swap(from.mCells);
        mStatics = from.mStatics;

        from.clear();
    }

    void CellReturns::clear()
    {
        mCells.clear();
        mModels.clear();
        mTextures.clear();
    }

    void CellReturns::take(CellReturns& from)
    {
        mCells.insert(mCells.end(), from.mCells.begin(), from.mCells.end());
        mModels.insert(mModels.end(), from.mModels.begin(), from.mModels.end());
        mTextures.insert(mTextures.end(), from.mTextures.begin(), from.mTextures.end());

        from.clear();
    }

    CellSupply::CellSupply() = default;

    CellSupply::~CellSupply() = default;

    void CellSupply::follow(const CellWorld& world)
    {
        mOnFrame.check();

        // The thread reads the storages and the content without the lock, which is sound only
        // because it is stopped and joined here before any is replaced — and before the reader that
        // holds them goes. Nothing is given back: what the frame held dies with the reader.
        mWorker.stop();

        mWanted.clear();
        mRequested.clear();
        mReading.clear();
        mDone.clear();
        mReturned.clear();
        mReturning.clear();
        mRecycling.clear();
        mMeasured = ReaderMemory{};
        mReader.reset();

        mWorld = world;

        if (!mWorld.isReadable())
            return;

        mReader = std::make_unique<CellReader>(
            *mWorld.mStorage, *mWorld.mGround, *mWorld.mContent, mWorld.mWorldspace, mWorld.mMask);
        mWorker.start("cell reader", [this](const Platform::StopToken& stop) { work(stop); });
    }

    void CellSupply::ask(const CellRequest& request)
    {
        mOnFrame.check();

        if (mReader == nullptr || request == mRequested)
            return;

        mRequested = request;

        mMonitor.give([&] {
            mWanted = request;
            ++mAsked;
        });
    }

    void CellSupply::take(std::vector<PreparedCell*>& into)
    {
        mOnFrame.check();

        mMonitor.under([&] {
            into.insert(into.end(), mDone.begin(), mDone.end());
            mDone.clear();
        });
    }

    bool CellSupply::waitForOne()
    {
        mOnFrame.check();

        return mMonitor.await([&] { return !mDone.empty(); });
    }

    void CellSupply::publish()
    {
        mOnFrame.check();

        if (mReturning.empty())
            return;

        mMonitor.give([&] { mReturned.take(mReturning); });
    }

    ReaderMemory CellSupply::getReaderMemory()
    {
        mOnFrame.check();

        return mMonitor.under([&] { return mMeasured; });
    }

    void CellSupply::recycle()
    {
        for (PreparedCell* cell : mRecycling.mCells)
            mReader->giveBack(*cell);

        for (PreparedTexture* texture : mRecycling.mTextures)
            mReader->giveBack(*texture);

        for (PreparedModel* model : mRecycling.mModels)
            mReader->giveBack(*model);

        mRecycling.clear();
    }

    void CellSupply::work(const Platform::StopToken& stop)
    {
        mMonitor.serve(
            stop, [&] { return !mWanted.empty() || !mReturned.empty(); },
            [&] {
                mRecycling.take(mReturned);
                mReading.take(mWanted);
                mReadingAsked = mAsked;
            },
            [&](const Platform::StopToken& turn) {
                recycle();
                read(turn);
            });
    }

    void CellSupply::read(const Platform::StopToken& stop)
    {
        for (const osg::Vec2i& cell : mReading.mCells)
        {
            if (stop.stopRequested())
                return;

            // A newer ask replaces this one: the eye has moved and what it lacks has changed. An ask
            // for nothing too, which says the list in flight is no longer wanted.
            const bool newer = mMonitor.under([&] {
                mRecycling.take(mReturned);
                return mAsked != mReadingAsked;
            });
            recycle();

            if (newer)
                return;

            const Crash::NoteScope noted("reading the cell {}, {}", cell.x(), cell.y());
            PreparedCell& made = mReader->read(cell, mReading.mStatics);
            const ReaderMemory measured = mReader->measure();

            mMonitor.hand([&] {
                mDone.push_back(&made);
                mMeasured = measured;
            });
        }
    }
}
