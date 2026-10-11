#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/amdgpu.hpp>
#include <apps/rtxtool/instruments/cardwatch.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <apps/rtxtool/instruments/nvml.hpp>
#include <components/rtx/common/cardaddress.hpp>

namespace RtxTool
{
    namespace
    {
        /// A tally counts every sample, tells its own process's from the rest, keeps a process's
        /// first name, and orders the others so two runs print the same line.
        TEST(RtxCardTallyTest, theOthersAreCountedByProcessAndOrderedMostFirst)
        {
            CardTally tally(100);

            // Seven samples: three of this process, two of 7, one each of 12 and 30, with 30 seen
            // once under one name and 12 renamed on its second sample.
            tally.take(100, "openmw-rtxtool");
            tally.take(7, "kwin_wayland");
            tally.take(100, "openmw-rtxtool");
            tally.take(30, "brave");
            tally.take(12, "zed-editor");
            tally.take(7, "kwin_wayland");
            tally.take(100, "openmw-rtxtool");

            // A name is asked for a process not this one and not yet seen, and for no other.
            EXPECT_FALSE(tally.needsName(100)) << "this process";
            EXPECT_FALSE(tally.needsName(7)) << "a process already named";
            EXPECT_TRUE(tally.needsName(41));

            const CardShare share = tally.summarise(2.5);
            EXPECT_TRUE(share.mViewed);
            EXPECT_DOUBLE_EQ(share.mSeconds, 2.5);
            EXPECT_EQ(share.mSamples, 7u);
            EXPECT_EQ(share.mOthers, 4u) << "7 twice, 12 once, 30 once";

            // Most samples first, and among equals by name: brave before zed-editor.
            ASSERT_EQ(share.mHolders.size(), 3u);
            EXPECT_EQ(share.mHolders[0].mName, "kwin_wayland");
            EXPECT_EQ(share.mHolders[0].mSamples, 2u);
            EXPECT_EQ(share.mHolders[1].mName, "brave");
            EXPECT_EQ(share.mHolders[1].mSamples, 1u);
            EXPECT_EQ(share.mHolders[2].mName, "zed-editor");
            EXPECT_EQ(share.mHolders[2].mSamples, 1u);

            EXPECT_EQ(describeCard(share),
                "card ran another process's work in 4 of 7 samples over 2.5 s: kwin_wayland 2, brave 1, zed-editor 1");

            // A process is named by its first sample, so one that is gone by the report — which
            // the driver then calls by number — is still the name it had.
            tally.take(12, "pid 12");
            EXPECT_EQ(tally.summarise(3.0).mHolders[0].mName, "kwin_wayland");
            EXPECT_EQ(tally.summarise(3.0).mHolders[1].mName, "zed-editor") << "two samples now, and its first name";

            // Cleared, the tally forgets the samples and keeps its process.
            tally.clear();
            tally.take(100, "openmw-rtxtool");
            const CardShare alone = tally.summarise(1.0);
            EXPECT_EQ(alone.mSamples, 1u);
            EXPECT_EQ(alone.mOthers, 0u);
            EXPECT_TRUE(alone.mHolders.empty());
            EXPECT_EQ(describeCard(alone), "card ran no other process's work, 1 sample over 1.0 s");

            // A window the driver took no sample in claims nothing about whose work the card ran.
            tally.clear();
            EXPECT_EQ(describeCard(tally.summarise(0.03)), "card not sampled over 0.0 s");

            // A share nothing looked at says why where it can.
            CardShare blind;
            EXPECT_EQ(describeCard(blind), "card not watched");
            blind.mWhyNot = "the driver keeps no process samples for this device";
            EXPECT_EQ(describeCard(blind), "card not watched: the driver keeps no process samples for this device");
        }

        /// amdgpu's files read as the driver writes them: the marked level of a clock, the sleep
        /// level's "S" among them, and millidegrees rounded to the degree.
        TEST(RtxAmdGpuTest, theClockIsTheMarkedLevelAndTheTemperatureIsRounded)
        {
            EXPECT_EQ(AmdGpu::currentLevelMhz("0: 500Mhz \n1: 1600Mhz *\n2: 2482Mhz \n"), 1600u);
            EXPECT_EQ(AmdGpu::currentLevelMhz("S: 800Mhz *\n0: 500Mhz \n1: 2482Mhz \n"), 800u);
            EXPECT_EQ(AmdGpu::currentLevelMhz("0: 96Mhz \n1: 456Mhz \n2: 1250Mhz *"), 1250u)
                << "the last line with no line break after it";
            EXPECT_EQ(AmdGpu::currentLevelMhz("0: 500Mhz \n1: 2482Mhz \n"), std::nullopt) << "no level marked";
            EXPECT_EQ(AmdGpu::currentLevelMhz(""), std::nullopt);
            EXPECT_EQ(AmdGpu::currentLevelMhz("*: nothing\n"), std::nullopt) << "a marked line with no number";

            EXPECT_EQ(AmdGpu::wholeDegrees("54000\n"), 54u);
            EXPECT_EQ(AmdGpu::wholeDegrees("54499"), 54u);
            EXPECT_EQ(AmdGpu::wholeDegrees("54500"), 55u);
            EXPECT_EQ(AmdGpu::wholeDegrees("0"), 0u);
            EXPECT_EQ(AmdGpu::wholeDegrees("hot"), std::nullopt);
        }

        /// A process is named by its executable alone, whatever else the driver hands back.
        TEST(RtxNvmlTest, aProcessIsNamedByItsExecutable)
        {
            EXPECT_EQ(Nvml::executableOf("/usr/bin/kwin_wayland"), "kwin_wayland");
            EXPECT_EQ(Nvml::executableOf("./components-tests"), "components-tests");
            EXPECT_EQ(Nvml::executableOf("zed-editor"), "zed-editor");
            EXPECT_EQ(Nvml::executableOf(R"(C:\Program Files\Zed\zed.exe)"), "zed.exe") << "a space in a directory";

            // A browser's GPU process titles itself with its whole command line, whose last
            // slash is inside an argument and whose first option is where the path ends.
            EXPECT_EQ(Nvml::executableOf("/opt/brave-bin/brave --type=gpu-process "
                                         "--render-node-override=/dev/dri/renderD128 --crashpad-handler-pid=1"),
                "brave");
            EXPECT_EQ(Nvml::executableOf(""), "");
        }

        /// The two spellings of one address: the kernel's folder with four digits of domain, and
        /// NVML's bus id with eight, which a domain past four digits still fits.
        TEST(RtxCardAddressTest, eachReaderSpellsTheAddressItsOwnWay)
        {
            const Rtx::PciAddress first{ .mDomain = 0, .mBus = 1, .mDevice = 0, .mFunction = 0 };
            EXPECT_EQ(AmdGpu::folderName(first), "0000:01:00.0");
            EXPECT_EQ(Nvml::busId(first), "00000000:01:00.0");

            const Rtx::PciAddress far{ .mDomain = 0x10000, .mBus = 0xa, .mDevice = 0x1f, .mFunction = 7 };
            EXPECT_EQ(AmdGpu::folderName(far), "10000:0a:1f.7");
            EXPECT_EQ(Nvml::busId(far), "00010000:0a:1f.7");
        }

        /// **A card the watch cannot read is not read, and each window says why**: none named, one
        /// with no address, and one of a vendor no reader reads.
        TEST(RtxCardWatchTest, aCardItCannotReadIsNotReadAndTheReportSaysWhy)
        {
            const auto why = [](CardWatch& watch) {
                const CardWindows windows = watch.stop();
                EXPECT_FALSE(windows.mPlace.mClock.mRead);
                EXPECT_FALSE(windows.mPlace.mShare.mViewed);
                return describeCard(windows.mPlace.mShare);
            };

            CardWatch unnamed(std::chrono::milliseconds(5));
            EXPECT_EQ(why(unnamed), "card not watched: no card was named");

            CardWatch unaddressed(std::chrono::milliseconds(5));
            unaddressed.watch(Rtx::CardAddress{ .mVendor = 0x10de, .mPci = std::nullopt });
            EXPECT_EQ(why(unaddressed), "card not watched: the renderer's device names no PCI address");

            CardWatch unread(std::chrono::milliseconds(5));
            unread.watch(Rtx::CardAddress{ .mVendor = 0x8086, .mPci = Rtx::PciAddress{} });
            EXPECT_EQ(why(unread), "card not watched: no instrument reads this vendor's cards");
        }
    }
}
