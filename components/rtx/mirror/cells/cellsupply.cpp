#include "cellsupply.hpp"

#include <cstddef>
#include <optional>
#include <vector>

#include <components/crashcatcher/crashnote.hpp>

#include "cellreader.hpp"
#include "prepared.hpp"

namespace Rtx
{
    void CellReturns::clear()
    {
        mCells.clear();
        mGrass.clear();
        mModels.clear();
        mTextures.clear();
    }

    void CellReturns::take(CellReturns& from)
    {
        mCells.insert(mCells.end(), from.mCells.begin(), from.mCells.end());
        mGrass.insert(mGrass.end(), from.mGrass.begin(), from.mGrass.end());
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
        mWorker = std::jthread();

        mTable.clear();
        mReturned.clear();
        mReturning.clear();
        mRecycling.clear();
        mPicked.reset();
        mMeasured = ReaderMemory{};
        mReader.reset();

        mWorld = world;

        if (!mWorld.isReadable())
            return;

        mReader = std::make_unique<CellReader>(mWorld);
        mWorker = Platform::startThread("cell reader", [this](const std::stop_token& stop) { work(stop); });
    }

    void CellSupply::retarget(const CellTarget& target, CellsDropped& dropped)
    {
        mOnFrame.check();

        mMonitor.give([&] {
            mTable.retarget(target, mReturning, dropped);
            return true;
        });
    }

    void CellSupply::withdraw()
    {
        mOnFrame.check();

        mMonitor.under([&] { mTable.withdraw(); });
    }

    void CellSupply::awaitOwed(const ReadKind kind)
    {
        mOnFrame.check();

        // The records stay where they stand through the wait: only the frame orders or erases them.
        const std::optional<std::size_t> owed = mMonitor.under([&] { return mTable.findOwed(kind); });
        if (owed.has_value())
            mMonitor.await([&] { return mTable.isRead(*owed, kind); });
    }

    void CellSupply::collectFiled(std::vector<PreparedCell*>& cells, std::vector<PreparedGrass*>& grass)
    {
        mOnFrame.check();

        mMonitor.under([&] { mTable.collectFiled(cells, grass); });
    }

    PreparedCell* CellSupply::adoptCell()
    {
        mOnFrame.check();

        PreparedCell* adopted = nullptr;
        mMonitor.give([&] {
            adopted = mTable.adoptCell();
            return adopted != nullptr;
        });
        return adopted;
    }

    PreparedGrass* CellSupply::adoptGrass()
    {
        mOnFrame.check();

        PreparedGrass* adopted = nullptr;
        mMonitor.give([&] {
            adopted = mTable.adoptGrass();
            return adopted != nullptr;
        });
        return adopted;
    }

    std::size_t CellSupply::countStood(const ReadKind kind)
    {
        mOnFrame.check();

        return mMonitor.under([&] { return mTable.countStood(kind); });
    }

    void CellSupply::publish()
    {
        mOnFrame.check();

        if (mReturning.empty())
            return;

        mMonitor.give([&] {
            mReturned.take(mReturning);
            return true;
        });
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

        for (PreparedGrass* grass : mRecycling.mGrass)
            mReader->giveBack(*grass);

        for (PreparedTexture* texture : mRecycling.mTextures)
            mReader->giveBack(*texture);

        for (PreparedModel* model : mRecycling.mModels)
            mReader->giveBack(*model);

        mRecycling.clear();
    }

    void CellSupply::work(const std::stop_token& stop)
    {
        mMonitor.serve(
            stop, [&] { return !mReturned.empty() || mTable.canPick(); },
            [&] {
                mRecycling.take(mReturned);
                mPicked = mTable.pick();
            },
            [&](const std::stop_token& turn) {
                recycle();
                if (mPicked.has_value())
                    read(*mPicked, turn);
            });
    }

    void CellSupply::read(const CellPick& pick, const std::stop_token& stop)
    {
        // What a band let go of while it was in hand goes back as the frame's own returns do, every hold the reader
        // lent it with it, and is never handed.
        if (pick.mKind == ReadKind::Grass)
        {
            const Crash::NoteScope noted("reading the groundcover of the cell {}, {}", pick.mCell.x(), pick.mCell.y());
            PreparedGrass& made = mReader->readGrass(pick.mCell, stop);
            if (stop.stop_requested())
                return;
            const ReaderMemory measured = mReader->measure();
            bool filed = false;
            mMonitor.hand([&] {
                filed = mTable.file(pick, made);
                mMeasured = measured;
            });
            if (!filed)
            {
                mRecycling.mModels.assign(made.mModels.begin(), made.mModels.end());
                mRecycling.mGrass.push_back(&made);
                recycle();
            }
            return;
        }

        const Crash::NoteScope noted("reading the cell {}, {}", pick.mCell.x(), pick.mCell.y());
        PreparedCell& made = mReader->read(pick.mCell, stop);
        if (stop.stop_requested())
            return;
        const ReaderMemory measured = mReader->measure();
        bool filed = false;
        mMonitor.hand([&] {
            filed = mTable.file(pick, made);
            mMeasured = measured;
        });
        if (!filed)
        {
            mRecycling.mModels.assign(made.mModels.begin(), made.mModels.end());
            made.mGround.collectTextures(mRecycling.mTextures);
            mRecycling.mCells.push_back(&made);
            recycle();
        }
    }
}
