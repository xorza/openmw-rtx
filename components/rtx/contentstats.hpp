#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "contentpass.hpp"

namespace Rtx
{
    /// What one pass cost, over however many times it was asked.
    struct PassStats
    {
        /// How often it was asked, and how many of those its cache answered.
        std::uint32_t mAsked = 0;
        std::uint32_t mHits = 0;

        /// What making the keys cost — every byte of the input read, and a texture described to
        /// read it — and what the runs cost past that.
        double mKeyMs = 0.0;
        double mRunMs = 0.0;

        /// How many bytes of input the keys were made of.
        std::uint64_t mKeyBytes = 0;

        PassStats& operator+=(const PassStats& other);
    };

    /// What every pass cost, by pass.
    struct ContentStats
    {
        std::array<PassStats, sContentPassCount> mPasses{};

        PassStats& at(ContentPassId pass) { return mPasses[static_cast<std::size_t>(pass)]; }
        const PassStats& at(ContentPassId pass) const { return mPasses[static_cast<std::size_t>(pass)]; }

        /// The whole of it, keys and runs: what the thread that asked spent.
        double getMs() const;

        ContentStats& operator+=(const ContentStats& other);
    };

    /// What was preprocessed, by the thread that did it: the frame's — what its walks met the first
    /// time, and what the host read between them — and the cell ring's reader, whose models the
    /// walk adopted. Apart, because only the first is a frame's time.
    struct Preprocessed
    {
        ContentStats mOnFrame;
        ContentStats mOffFrame;

        Preprocessed& operator+=(const Preprocessed& other);
    };
}
