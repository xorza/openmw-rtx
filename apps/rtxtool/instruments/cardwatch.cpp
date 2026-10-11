#include "cardwatch.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>

#include <components/platform/process.hpp>

namespace RtxTool
{
    namespace
    {
        /// The PCI vendors whose cards a reader reads.
        constexpr std::uint32_t sNvidia = 0x10de;
        constexpr std::uint32_t sAmd = 0x1002;
    }

    void CardTally::take(const std::uint32_t pid, const std::string_view name)
    {
        ++mSamples;
        if (pid == mSelf)
            return;

        ++mOthers;

        const auto known = std::find_if(
            mHolders.begin(), mHolders.end(), [pid](const CardHolder& holder) { return holder.mPid == pid; });
        if (known != mHolders.end())
        {
            ++known->mSamples;
            return;
        }

        mHolders.push_back(CardHolder{ .mPid = pid, .mName = std::string(name), .mSamples = 1 });
    }

    bool CardTally::needsName(const std::uint32_t pid) const
    {
        return pid != mSelf && std::none_of(mHolders.begin(), mHolders.end(), [pid](const CardHolder& holder) {
            return holder.mPid == pid;
        });
    }

    void CardTally::clear()
    {
        mSamples = 0;
        mOthers = 0;
        mHolders.clear();
    }

    CardShare CardTally::summarise(const double seconds) const
    {
        CardShare share{
            .mSeconds = seconds,
            .mSamples = mSamples,
            .mOthers = mOthers,
            .mHolders = mHolders,
            .mViewed = true,
        };

        // Most samples first, and by name among equals, so two runs print the same line.
        std::sort(share.mHolders.begin(), share.mHolders.end(), [](const CardHolder& left, const CardHolder& right) {
            if (left.mSamples != right.mSamples)
                return left.mSamples > right.mSamples;
            return left.mName < right.mName;
        });

        return share;
    }

    CardWatch::CardWatch(const std::chrono::milliseconds period)
        : mPeriod(period)
        , mTally(Platform::Process::currentId())
        , mBegan(std::chrono::steady_clock::now())
    {
    }

    CardWatch::~CardWatch() = default;

    void CardWatch::watch(const Rtx::CardAddress& card)
    {
        if (mWorker.isRunning())
            return;

        open(card);
        mMonitor.under([&] { close(); });
        mWorker.repeat("card watch", mPeriod, [this] {
            mMonitor.under([this] {
                if (mStartAsked.load(std::memory_order_acquire))
                    beginAsked();
                else
                    read();
            });
        });
    }

    void CardWatch::open(const Rtx::CardAddress& card)
    {
        if (!card.mPci.has_value())
        {
            mUnread = "the renderer's device names no PCI address";
            return;
        }

        switch (card.mVendor)
        {
            case sNvidia:
                mNvml.emplace(*card.mPci);
                return;
            case sAmd:
                mAmdGpu = AmdGpu::at(std::filesystem::path(AmdGpu::sDevices), *card.mPci);
                if (!mAmdGpu.has_value())
                    mUnread = "amdgpu lists no card at the renderer's address";
                return;
            default:
                mUnread = "no instrument reads this vendor's cards";
                return;
        }
    }

    void CardWatch::start()
    {
        mStartAsked.store(true, std::memory_order_release);
    }

    CardWindows CardWatch::stop()
    {
        return mMonitor.under([&] {
            beginAsked();
            const CardReading place = close();
            return CardWindows{ .mBefore = mBefore, .mPlace = place };
        });
    }

    void CardWatch::beginAsked()
    {
        if (mStartAsked.exchange(false, std::memory_order_acq_rel))
            mBefore = close().mShare;
    }

    std::uint32_t CardWatch::getReadings()
    {
        return mMonitor.under([this] { return mClock.getReadings(); });
    }

    void CardWatch::read()
    {
        if (mAmdGpu.has_value())
            mClock.add(mAmdGpu->readClock());
        if (!mNvml.has_value())
            return;

        mClock.add(mNvml->readClock());
        mNvml->readSamples(mSamples);
        for (const CardSample& sample : mSamples)
        {
            if (!sample.mHeld)
                continue;

            if (mTally.needsName(sample.mPid))
                mNvml->nameProcess(sample.mPid, mName);
            mTally.take(sample.mPid, mName);
        }
    }

    CardReading CardWatch::close()
    {
        read();

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        CardReading reading{
            .mClock = mClock,
            .mShare = mTally.summarise(std::chrono::duration<double>(now - mBegan).count()),
        };
        if (!mNvml.has_value() || !mNvml->hasSamples())
        {
            reading.mShare.mViewed = false;
            reading.mShare.mWhyNot = mNvml.has_value() ? mNvml->describeUnsampled()
                : mAmdGpu.has_value()                  ? AmdGpu::describeUnsampled()
                                                       : mUnread;
        }

        mClock = GpuClock{};
        mTally.clear();
        mBegan = now;

        return reading;
    }

    std::string describeCard(const CardShare& share)
    {
        if (!share.mViewed)
            return share.mWhyNot.empty() ? std::string("card not watched")
                                         : std::format("card not watched: {}", share.mWhyNot);

        // A window the driver took no sample in says so rather than claiming quiet: it samples
        // five times a second, and a stop of two frames is over before it does.
        if (share.mSamples == 0)
            return std::format("card not sampled over {:.1f} s", share.mSeconds);

        const char* const samples = share.mSamples == 1 ? "sample" : "samples";
        if (share.mOthers == 0)
            return std::format(
                "card ran no other process's work, {} {} over {:.1f} s", share.mSamples, samples, share.mSeconds);

        std::string named;
        for (const CardHolder& holder : share.mHolders)
            named += std::format("{}{} {}", named.empty() ? "" : ", ", holder.mName, holder.mSamples);

        return std::format("card ran another process's work in {} of {} {} over {:.1f} s: {}", share.mOthers,
            share.mSamples, samples, share.mSeconds, named);
    }
}
