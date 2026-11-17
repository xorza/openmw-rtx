#include "contentstats.hpp"

namespace Rtx
{
    PassStats& PassStats::operator+=(const PassStats& other)
    {
        mAsked += other.mAsked;
        mHits += other.mHits;
        mKeyMs += other.mKeyMs;
        mFoundMs += other.mFoundMs;
        mRunMs += other.mRunMs;
        mKeyBytes += other.mKeyBytes;

        return *this;
    }

    double ContentStats::getMs() const
    {
        double total = 0.0;
        for (const PassStats& pass : mPasses)
            total += pass.mKeyMs + pass.mFoundMs + pass.mRunMs;

        return total;
    }

    ContentStats& ContentStats::operator+=(const ContentStats& other)
    {
        for (std::size_t at = 0; at < mPasses.size(); ++at)
            mPasses[at] += other.mPasses[at];
        mTexelReads += other.mTexelReads;

        return *this;
    }

    Preprocessed& Preprocessed::operator+=(const Preprocessed& other)
    {
        mOnFrame += other.mOnFrame;
        mOffFrame += other.mOffFrame;
        mPrepared += other.mPrepared;
        mLoading += other.mLoading;
        mDerived += other.mDerived;

        return *this;
    }

    TexelReads Preprocessed::getTexelReads() const
    {
        TexelReads reads = mOnFrame.mTexelReads;
        reads += mOffFrame.mTexelReads;
        reads += mPrepared.mTexelReads;
        reads += mLoading.mTexelReads;
        reads += mDerived.mTexelReads;
        return reads;
    }
}
