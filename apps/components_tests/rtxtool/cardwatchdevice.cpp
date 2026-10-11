#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/rtxtool/instruments/cardwatch.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <apps/rtxtool/instruments/nvml.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/common/cardaddress.hpp>

namespace RtxTool
{
    namespace
    {
        using RtxNvmlDeviceTest = Rtx::Testing::DeviceTest;
        using RtxCardWatchDeviceTest = Rtx::Testing::DeviceTest;

        /// The card the renderer runs on, as the session hands it to the watch.
        Rtx::CardAddress rendererCard()
        {
            return Rtx::Testing::getHarness().mDevice->getPhysicalDevice().getAddress();
        }

        /// The renderer's card's address, where it is NVIDIA's.
        std::optional<Rtx::PciAddress> nvidiaCard()
        {
            const Rtx::CardAddress card = rendererCard();
            return card.mVendor == 0x10de ? card.mPci : std::nullopt;
        }

        /// Whether the driver's library loaded at all, which a driver may ship without.
        bool loaded(const Nvml& nvml)
        {
            return nvml.isOpen() || nvml.describeAbsence() != "the driver's management library did not load";
        }

        /// What the driver's library answers for the renderer's card, opened at its address.
        ///
        /// **A skip and not a failure where no library answers**, on another vendor's card or a
        /// driver without it: the card's clock is instrumentation rather than a renderer. A library
        /// that loads and finds no card at the renderer's address is a failure.
        TEST_F(RtxNvmlDeviceTest, theLibraryAnswersAClockAndNamesThisProcess)
        {
            const std::optional<Rtx::PciAddress> card = nvidiaCard();
            if (!card.has_value())
                GTEST_SKIP() << "the renderer's card is no NVIDIA card with an address";
            Nvml nvml(*card);
            if (!loaded(nvml))
                GTEST_SKIP() << "no driver library on this machine: " << nvml.describeAbsence();
            ASSERT_TRUE(nvml.isOpen()) << nvml.describeAbsence();

            EXPECT_TRUE(nvml.describeAbsence().empty());

            // A graphics clock and a memory clock a card of the last decade could hold, which is
            // what says the fields were read in the order they were asked for rather than shuffled.
            const GpuClock clock = nvml.readClock();
            ASSERT_TRUE(clock.mRead);
            EXPECT_GT(clock.mCore.mLowestMhz, 100u);
            EXPECT_LT(clock.mCore.mLowestMhz, 10000u);
            EXPECT_EQ(clock.mCore.mLowestMhz, clock.mCore.mHighestMhz) << "one reading is not a range";
            EXPECT_EQ(clock.mMemory.mReadings, 1u);
            EXPECT_GT(clock.mMemory.mLowestMhz, 100u);
            ASSERT_TRUE(clock.mTemperatureC.has_value());
            EXPECT_GT(*clock.mTemperatureC, 0u);
            EXPECT_LT(*clock.mTemperatureC, 120u);
            EXPECT_TRUE(clock.mThrottleMask.has_value());

            // This process, by the name the driver keeps for it: the executable's own, without
            // its directory, which is what a report prints beside a count. The file's name as the
            // system spells it, so `rtx-gpu-tests` on Linux and `rtx-gpu-tests.exe` on Windows.
            const std::optional<std::filesystem::path> executable = Platform::Process::executable();
            ASSERT_TRUE(executable.has_value());
            std::string name;
            nvml.nameProcess(Platform::Process::currentId(), name);
            EXPECT_EQ(name, Files::pathToUnicodeString(executable->filename()));

            // One nobody runs is named by its number.
            nvml.nameProcess(4'000'000'000u, name);
            EXPECT_EQ(name, "pid 4000000000");

            // Whatever the driver has sampled since the library opened is newer than what it held
            // then, so nothing that came back predates the opening.
            if (nvml.hasSamples())
            {
                std::vector<CardSample> samples;
                nvml.readSamples(samples);
                for (const CardSample& sample : samples)
                    EXPECT_GT(sample.mStamp, 0u);
            }
        }

        /// A watch samples across the window it is open for, and every window starts from
        /// nothing.
        ///
        /// **The reading count is the whole claim for the clock.** Two samples printed as a range
        /// cannot be told from a card watched throughout, so what a watch has to prove is that it
        /// took more than two.
        TEST_F(RtxCardWatchDeviceTest, aWatchSamplesAcrossTheWindowAndEveryWindowStartsFromNothing)
        {
            const std::optional<Rtx::PciAddress> card = nvidiaCard();
            if (!card.has_value())
                GTEST_SKIP() << "the renderer's card is no NVIDIA card with an address";
            const Nvml probe(*card);
            if (!loaded(probe))
                GTEST_SKIP() << "no driver library on this machine";
            const bool sampled = probe.hasSamples();

            // A twentieth of the harness's period: what is claimed is a few turns of the loop, and
            // not how often it turns.
            CardWatch watch(std::chrono::milliseconds(5));
            watch.watch(rendererCard());

            // **Waited for and not slept out.** What the claim needs is a few turns of the watch's
            // own loop, and how long those take is the machine's to say.
            const auto waitFor = [&](std::uint32_t readings) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (watch.getReadings() < readings && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            };

            waitFor(2);
            ASSERT_GE(watch.getReadings(), 2u) << "the watch's loop never came round";

            // A second `watch` is nothing at all: the window goes on counting.
            watch.watch(rendererCard());
            EXPECT_GE(watch.getReadings(), 2u);

            // **Starting a place's window asks and waits on nothing**: the watch's next turn closes
            // the window between places and begins the place's, which is waited for, not timed.
            watch.start();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!watch.hasBegun() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ASSERT_TRUE(watch.hasBegun()) << "the watch's loop never began the window";

            waitFor(3);
            const CardWindows windows = watch.stop();
            EXPECT_EQ(windows.mBefore.mViewed, sampled);
            EXPECT_GT(windows.mBefore.mSeconds, 0.0);

            const CardReading& place = windows.mPlace;
            EXPECT_TRUE(place.mClock.mRead);
            EXPECT_GT(place.mClock.getReadings(), 2u) << "a watch that answered with no more than its two ends";
            EXPECT_GE(place.mClock.mCore.getMeanMhz(), place.mClock.mCore.mLowestMhz);
            EXPECT_LE(place.mClock.mCore.getMeanMhz(), place.mClock.mCore.mHighestMhz);
            EXPECT_EQ(place.mShare.mViewed, sampled);
            EXPECT_GT(place.mShare.mSeconds, 0.0);

            // **A window `stop` ends before any turn began it is begun there**, so a place two
            // frames long still has a window between places and one of its own.
            watch.start();
            const CardWindows hurried = watch.stop();
            EXPECT_TRUE(hurried.mPlace.mClock.mRead) << "the place's own last reading";
            EXPECT_TRUE(watch.hasBegun());
        }
    }
}
