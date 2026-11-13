#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Rtx
{
    /// Where a pipeline came from, as the driver's creation feedback says.
    enum class PipelineSource : std::uint8_t
    {
        /// Compiled, past the pipeline cache.
        Compiled,
        /// Found in the pipeline cache.
        Cached,
        /// The driver gave no feedback.
        Unsaid,
    };

    /// What the driver said of making a pipeline (`VkPipelineCreationFeedback`).
    struct PipelineFeedback
    {
        PipelineSource mSource = PipelineSource::Unsaid;

        /// How long the making took, compiled or found; nought where the driver said nothing.
        double mDurationMs = 0.0;
    };

    /// What the driver's statistics say of a pipeline, where the device asks for them
    /// (`PipelineStatistics::Captured`).
    struct PipelineStatisticsSaid
    {
        std::uint32_t mExecutables = 0;

        /// The most scratch memory a lane of any of its executables uses, in bytes
        /// (`CompiledUse::mScratchBytes`).
        std::uint64_t mScratchBytes = 0;
    };

    /// One pipeline as the device reported it (`Device::reportPipeline`).
    struct PipelineMade
    {
        std::string_view mName;
        PipelineFeedback mFeedback;
        std::optional<PipelineStatisticsSaid> mStatistics;
    };

    struct PipelineSpill
    {
        std::string mName;
        std::uint64_t mBytes = 0;
    };

    struct PipelineSlowest
    {
        std::string mName;
        double mMs = 0.0;
    };

    /// What the pipelines made over a stretch came to (`PipelineTally::take`).
    struct PipelineSummary
    {
        std::uint32_t mMade = 0;
        std::uint32_t mCompiled = 0;
        std::uint32_t mCached = 0;

        /// The compiled pipelines' durations, summed, and the longest of them.
        double mCompileMs = 0.0;
        std::optional<PipelineSlowest> mSlowest;

        /// How many were made with statistics, and how many of those the driver made of no executable
        /// it reports: NVIDIA's ray tracing pipelines.
        std::uint32_t mWithStatistics = 0;
        std::uint32_t mWithoutExecutable = 0;

        /// Every pipeline whose lanes use scratch memory, the most first, then by name.
        std::vector<PipelineSpill> mSpills;

        /// From the stretch's start to the last pipeline made in it.
        std::chrono::duration<double> mSpan{};
    };

    /// What the pipelines a device makes came to, counted as each is made, so a start's hundreds are
    /// said in a line or two. Under a lock, because the kernels compile on several hands at once.
    class PipelineTally
    {
    public:
        explicit PipelineTally(std::chrono::steady_clock::time_point start);

        void add(const PipelineMade& made, std::chrono::steady_clock::time_point at);

        /// What was added since the start or the last take; the next stretch starts at `now`.
        PipelineSummary take(std::chrono::steady_clock::time_point now);

    private:
        std::mutex mLock;
        std::chrono::steady_clock::time_point mStart;
        PipelineSummary mSummary;
    };

    /// The line a summary is logged as.
    std::string describePipelines(const PipelineSummary& summary);

    /// The warning a summary's spills are logged as, empty where none spills.
    std::string describeSpills(const PipelineSummary& summary);
}
