#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/screenwait.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/rtx/common/jobprogress.hpp>

namespace MWRender
{
    namespace
    {
        /// A loading screen that writes down what it is told.
        struct Screen final : Loading::Listener
        {
            std::string mLabel;
            std::vector<std::string> mNotes;
            std::vector<std::size_t> mRanges;
            std::vector<std::size_t> mIncreases;
            std::uint32_t mSets = 0;

            void setLabel(const std::string& label, bool /*important*/) override { mLabel = label; }
            void setNote(const std::string_view note) override { mNotes.emplace_back(note); }
            void setProgressRange(const std::size_t range) override { mRanges.push_back(range); }
            void setProgress(std::size_t /*value*/) override { ++mSets; }
            void increaseProgress(const std::size_t increase) override { mIncreases.push_back(increase); }
        };

        /// A job that answers the progress scripted for each ask, the last for every ask after, and writes down the
        /// patience it was asked with.
        struct Job
        {
            std::vector<Rtx::JobProgress> mAnswers;
            std::vector<std::chrono::milliseconds> mPatience = {};

            Rtx::JobProgress await(const std::chrono::milliseconds patience)
            {
                const std::size_t at = std::min(mPatience.size(), mAnswers.size() - 1);
                mPatience.push_back(patience);
                return mAnswers[at];
            }
        };

        ScreenWaitAsk askOf(Job& job, std::uint32_t& quitAsks, const std::uint32_t quitsAt, const std::string_view note)
        {
            return ScreenWaitAsk{ .mLabel = "a label",
                .mNote = note,
                .mAwait = [&job](const std::chrono::milliseconds patience) { return job.await(patience); },
                .mQuits = [&quitAsks, quitsAt] { return ++quitAsks >= quitsAt; } };
        }

        constexpr std::uint32_t sNever = 1000;

        /// **Every report asks the screen for a draw, the count moved or not**, by what the count moved: a job of
        /// five that stands at one for two reports, then at four, then done, is increases of 1, 0, 3 and 1, and no
        /// `setProgress`, which draws only where the bar moved a pixel. The first ask waits for nothing and each
        /// after for the patience; the note is set and taken back; a job done at the first ask touches no screen.
        TEST(RtxScreenWaitTest, everyReportAsksForADrawAndAQuitLeavesTheWait)
        {
            using Progress = Rtx::JobProgress;
            using std::chrono::milliseconds;

            Screen screen;
            Job job{ .mAnswers = { Progress{ 1, 5 }, Progress{ 1, 5 }, Progress{ 4, 5 }, Progress{ 5, 5 } } };
            std::uint32_t quitAsks = 0;
            EXPECT_EQ(awaitOnScreen(screen, askOf(job, quitAsks, sNever, "a note")), ScreenWait::Done);
            EXPECT_EQ(screen.mLabel, "a label");
            EXPECT_EQ(screen.mNotes, (std::vector<std::string>{ "a note", "" }));
            EXPECT_EQ(screen.mRanges, (std::vector<std::size_t>{ 5 }));
            EXPECT_EQ(screen.mIncreases, (std::vector<std::size_t>{ 1, 0, 3, 1 }));
            EXPECT_EQ(screen.mSets, 0u);
            EXPECT_EQ(job.mPatience,
                (std::vector<milliseconds>{ milliseconds(0), milliseconds(8), milliseconds(8), milliseconds(8) }));
            EXPECT_EQ(quitAsks, 3u) << "asked once a report that left the job running";

            // **A quit asked leaves the wait with the job running**, after the draw that read it and before another
            // patience: at the second report here, so two draws and one patience waited.
            Screen left;
            Job endless{ .mAnswers = { Progress{ 0, 9 } } };
            quitAsks = 0;
            EXPECT_EQ(awaitOnScreen(left, askOf(endless, quitAsks, 2, {})), ScreenWait::Quit);
            EXPECT_EQ(left.mIncreases, (std::vector<std::size_t>{ 0, 0 }));
            EXPECT_EQ(endless.mPatience, (std::vector<milliseconds>{ milliseconds(0), milliseconds(8) }));
            EXPECT_TRUE(left.mNotes.empty()) << "no note was asked for";

            Screen untouched;
            Job done{ .mAnswers = { Progress{ 3, 3 } } };
            quitAsks = 0;
            EXPECT_EQ(awaitOnScreen(untouched, askOf(done, quitAsks, 1, "a note")), ScreenWait::Done);
            EXPECT_TRUE(untouched.mLabel.empty());
            EXPECT_TRUE(untouched.mRanges.empty());
            EXPECT_TRUE(untouched.mIncreases.empty());
            EXPECT_EQ(quitAsks, 0u);
        }
    }
}
