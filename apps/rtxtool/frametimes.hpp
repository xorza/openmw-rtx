#pragma once

#include <vector>

namespace RtxTool
{
    /// What a run of frame times came to, in milliseconds.
    ///
    /// **A distribution and not an average**, because the two questions a renderer gets asked are
    /// different ones. "Is this build faster than that one" is answered by the middle of the run;
    /// "does this place play badly" is answered by its tail, and a mean hides exactly the frames
    /// that make a picture stutter. Both are here so neither has to be inferred from the other.
    struct FrameTimes
    {
        double mMean = 0.0;
        double mMedian = 0.0;

        /// The frame that only one in twenty, and one in a hundred, are worse than.
        double mP95 = 0.0;
        double mP99 = 0.0;

        double mBest = 0.0;
        double mWorst = 0.0;

        /// Frames a second, were every frame the median one.
        double getRate() const;

        /// Frames a second at the ninety-ninth percentile — the "one per cent low" a frame rate is
        /// usually quoted with, and the number that says whether a run was smooth.
        double getLowRate() const;
    };

    /// Sorts `times` and summarises it. At least one time, which every caller has by construction.
    ///
    /// **Nearest rank, which is a sample and never an interpolation between two.** Every figure
    /// here is a frame that actually happened, so a percentile can be looked up in the run that
    /// produced it: the qth is the `ceil(q * n)`th shortest, counting from one.
    ///
    /// **By reference, and it sorts in place.** A copy would read better at the call site and
    /// cannot be had: taking one loses the compiler its proof that the caller's loop pushed at
    /// least once, and indexing a vector it can no longer see into is a hard warning.
    FrameTimes summarise(std::vector<double>& times);
}
