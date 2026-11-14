#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <apps/rtxtool/instruments/framehashes.hpp>
#include <components/misc/result.hpp>

#include "benchrecord.hpp"
#include "benchrun.hpp"
#include "exitstatus.hpp"

namespace RtxTool
{
    /// What a run comes to as it happens: the places measured, the header they stood under, the report as it is
    /// built, and whether anything failed. One type, because a run has one verdict and one account of itself: a
    /// writer that wrote a line and forgot a separately held status reported a failure as a success. Here rather
    /// than beside the driver, because none of it needs a world.
    class RunRecord
    {
    public:
        void reserve(std::size_t places) { mPlaces.reserve(places); }

        /// Takes the premises the run states before its first frame into the header: the build,
        /// what the command measures and hashes, and what the renderer is made with. What only a
        /// frame says — the extents, the reconstruction resolved — the first stop adds.
        void begin(const SessionRequest& request);

        /// Whether no place has been measured yet, which is what says the header is still to be
        /// taken.
        bool empty() const { return mPlaces.empty(); }

        /// The header, for the one caller that fills it: what the whole run stood under is settled
        /// at the first place, because every place of a run is traced by one renderer.
        BenchHeader& getHeader() { return mHeader; }

        /// Takes one measured place, and prints it into the report: under the header, for the first.
        void add(BenchPlace place);

        /// Adds to the report.
        void note(std::string_view text) { mReport += text; }

        /// Says the run failed, whatever else it wrote.
        ///
        /// Apart from `note`, because a line and a verdict are two statements: a picture that could not be written
        /// says so and fails the run; one that was written says so and does not.
        void fail() { mStatus = worseOf(mStatus, ExitStatus::Failed); }

        /// Says a hashed frame differed from its reference: the run's status unless it failed.
        void differ() { mStatus = worseOf(mStatus, ExitStatus::Differed); }

        /// Says `stop` was passed over because the content does not hold what it names, `why`, and fails the run
        /// where the stop says so (`WhenAbsent`).
        void skip(const Stop& stop, std::string_view why);

        /// `stop` failed as it ran, for `why`, and the run went on past it (`WhenAStopFails::GoesOn`): said, and the
        /// run fails at its end.
        void fail(const Stop& stop, std::string_view why);

        /// Counts `check` and whether it held, keeping one that did not until `takeFailedChecks`.
        void checked(Check check, bool held);

        /// Writes the record as it stands where the request names one (`SessionRequest::mJson`), as a long run does
        /// after every stop so one that dies says how far it came; says why where it could not be written.
        void save(const SessionRequest& request);

        /// The places recorded so far, the last the stop that just ended.
        const std::vector<BenchPlace>& getPlaces() const { return mPlaces; }

        /// The checks that failed since this was last asked, for a run that turns each into a finding of its own.
        std::vector<Check> takeFailedChecks() { return std::exchange(mFailedChecks, {}); }

        /// The hashes this run took, and the ones it is being compared against.
        ///
        /// The run's own record of what it drew: a hash is one line of the report at the end and one number per
        /// measured frame until then, so it settles here rather than beside the frames.
        FrameHashes& getHashes() { return mHashes; }
        void readReference(const std::filesystem::path& path) { mReference = FrameHashes::read(path); }

        const std::string& getReport() const { return mReport; }

        /// Closes the run: the total under the places, the check tally, the hashes and the record.
        ///
        /// A file it cannot write is a line of the report and a failed run, never a throw: it runs inside the
        /// engine's frame, and a throw from there would lose every measured place's figures. Takes the request
        /// rather than four paths, because what it writes and compares against is what the run was asked for.
        void finish(const SessionRequest& request);

        /// Closes a run that failed before its last stop, for the stops it reached: the frames
        /// whose pictures will not come are dropped, and the rest is closed as `finish` closes it.
        ///
        /// **Closed and not left open**, because a record that was not written leaves the last
        /// run's at its path, to be compared as this one's.
        void abandon(const SessionRequest& request);

        /// Everything a launcher reads back, with `left` where the eye was, or null where the run
        /// reached no place.
        ///
        /// The whole result and never a field at a time: a launcher reads six of these, and an unfilled
        /// `SessionResult` is a valid one describing a camera at the origin, so a member-by-member hand-over loses
        /// whichever nobody remembered, silently. Hands the kept pictures over, so it is asked once.
        SessionResult conclude(const Stop* left);

        /// Where every picture a stop writes is kept, where the request keeps them, or null.
        PictureShelf* getShelf() { return mKeepsPictures ? &mShelf : nullptr; }

        /// Notes the extents the renderer made, which the result carries.
        void noteTraced(const Rtx::FrameExtents& traced) { mTraced = traced; }

    private:
        /// Whether `written` holds; where not, says why in the report and fails the run.
        bool wrote(const Result<void, std::string>& written);

        std::vector<BenchPlace> mPlaces;
        BenchHeader mHeader;

        /// The report as it is built, so a launcher gets the whole of it rather than the log's
        /// timestamped halves.
        std::string mReport;

        /// The kernel's compaction stalls when the first place loaded (`Platform::Process::compactionStalls`):
        /// a run on huge pages whose count rose has frames a compaction stalled.
        std::optional<std::uint64_t> mCompactionStalls;

        ExitStatus mStatus = ExitStatus::Passed;

        bool mKeepsPictures = false;
        PictureShelf mShelf;
        std::optional<Rtx::FrameExtents> mTraced;

        /// How many checks the run asked and how many of them failed.
        std::uint32_t mChecked = 0;
        std::uint32_t mFailed = 0;
        std::vector<Check> mFailedChecks;

        FrameHashes mHashes;
        FrameHashes mReference;
    };
}
