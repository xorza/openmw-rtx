#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace MWRender
{
    namespace
    {
        /// Sets an environment variable for a scope and puts back what it held, nothing as empty,
        /// which the renderer reads as nothing asked.
        class EnvironmentFor
        {
        public:
            EnvironmentFor(const char* name, const char* value)
                : mName(name)
                , mWas(std::getenv(name) != nullptr ? std::getenv(name) : "")
            {
                Platform::Process::setEnvironment(name, value);
            }

            ~EnvironmentFor() { Platform::Process::setEnvironment(mName, mWas.c_str()); }

            EnvironmentFor(const EnvironmentFor&) = delete;
            EnvironmentFor& operator=(const EnvironmentFor&) = delete;
            EnvironmentFor(EnvironmentFor&&) = delete;
            EnvironmentFor& operator=(EnvironmentFor&&) = delete;

        private:
            const char* mName;
            std::string mWas;
        };

        /// **The environment raises the layers and never lowers them**: nothing asked leaves the
        /// build's options as they are, `0` and empty ask nothing, sync raises off to sync and keeps
        /// a higher level, the GPU-assisted level replaces either, and the submit watch and the
        /// checkpoints are each turned on by their own name alone.
        TEST(RtxRunSetupTest, theEnvironmentRaisesTheLayersAndNeverLowersThem)
        {
            const EnvironmentFor sync("OPENMW_RTX_SYNC_VALIDATION", "");
            const EnvironmentFor gpu("OPENMW_RTX_GPU_VALIDATION", "");
            const EnvironmentFor watch("OPENMW_RTX_WATCH_SUBMITS", "0");
            const EnvironmentFor crumbs("OPENMW_RTX_BREADCRUMBS", "");

            const Rtx::ValidationOptions built{ .mLevel = Rtx::ValidationLevel::Off };
            const Rtx::ValidationOptions kept = askedValidation(built);
            EXPECT_EQ(kept.mLevel, Rtx::ValidationLevel::Off);
            EXPECT_FALSE(kept.mWatchSubmits);
            EXPECT_EQ(kept.mCheckpoints, built.mCheckpoints);

            {
                const EnvironmentFor asked("OPENMW_RTX_SYNC_VALIDATION", "1");
                EXPECT_EQ(askedValidation(built).mLevel, Rtx::ValidationLevel::Sync);
                EXPECT_EQ(askedValidation({ .mLevel = Rtx::ValidationLevel::Gpu }).mLevel, Rtx::ValidationLevel::Gpu);
            }
            {
                const EnvironmentFor asked("OPENMW_RTX_GPU_VALIDATION", "yes");
                EXPECT_EQ(askedValidation(built).mLevel, Rtx::ValidationLevel::Gpu);
                EXPECT_EQ(askedValidation({ .mLevel = Rtx::ValidationLevel::Sync }).mLevel, Rtx::ValidationLevel::Gpu);
            }
            {
                const EnvironmentFor asked("OPENMW_RTX_WATCH_SUBMITS", "1");
                const Rtx::ValidationOptions watched = askedValidation(built);
                EXPECT_TRUE(watched.mWatchSubmits);
                EXPECT_EQ(watched.mLevel, Rtx::ValidationLevel::Off);
            }
            {
                const EnvironmentFor asked("OPENMW_RTX_BREADCRUMBS", "1");
                EXPECT_TRUE(askedValidation({ .mCheckpoints = false }).mCheckpoints);
            }
        }

        /// **A played session's backend and store**: the device's backend where nothing names
        /// another, the null one headless where `OPENMW_RTX_BACKEND` names it, and the content store
        /// in its folder under the cache path, or none for no cache path.
        TEST(RtxRunSetupTest, aPlayedSessionsBackendIsTheEnvironmentsAndItsStoreUnderTheCache)
        {
            {
                const EnvironmentFor none("OPENMW_RTX_BACKEND", "");
                EXPECT_EQ(askedBackend(), RtxBackend::Vulkan);
                const RunSetup played = playedRunSetup("cache");
                EXPECT_EQ(played.mBackend, RtxBackend::Vulkan);
                EXPECT_FALSE(played.mHeadless);
                EXPECT_EQ(played.mContentStore, std::optional(std::filesystem::path("cache") / "rtx-content"));
                EXPECT_EQ(playedRunSetup({}).mContentStore, std::nullopt);
                EXPECT_EQ(played.mRun.mProfile.mRadianceWidth, Rtx::RadianceWidth::Shown);
            }
            {
                const EnvironmentFor null("OPENMW_RTX_BACKEND", "null");
                const RunSetup played = playedRunSetup({});
                EXPECT_EQ(played.mBackend, RtxBackend::Null);
                EXPECT_TRUE(played.mHeadless);
            }
        }

        /// **The run as the log says it**, every field it names moved from a played session's
        /// defaults and each shown to change the line.
        TEST(RtxRunSetupTest, theLogSaysEveryKnobOfTheRun)
        {
            RunSetup played;
            played.mRun.mValidation.mMarkers = true;
            played.mRun.mValidation.mCheckpoints = false;
            const std::string build = Rtx::sValidationByDefault ? "" : "never ";
            EXPECT_EQ(describe(played),
                "backend vulkan, window shown, validation off, markers on, checkpoints off, upscale off by fsr, "
                "kernels drawing, content store off, video memory as the device states it, a build that "
                    + build + "validates by default");

            RunSetup measured = played;
            measured.mBackend = RtxBackend::Null;
            measured.mHeadless = true;
            measured.mRun.mValidation.mLevel = Rtx::ValidationLevel::Sync;
            measured.mRun.mValidation.mWatchSubmits = true;
            measured.mRun.mValidation.mMarkers = false;
            measured.mRun.mValidation.mCheckpoints = true;
            measured.mRun.mProfile.mUpscale = Rtx::Upscale::Quality;
            measured.mRun.mProfile.mUpscaler = Rtx::UpscalerChoice::Dlss;
            measured.mKernels = Rtx::KernelSet::Every;
            measured.mContentStore = std::filesystem::path("cache");
            measured.mRun.mMemoryBudget = std::uint64_t{ 4 } << 30;
            EXPECT_EQ(describe(measured),
                "backend null, window hidden, validation sync with every submit watched, markers off, checkpoints "
                "on, upscale quality by dlss, kernels every, content store on, video memory held to 4294967296 "
                "bytes, a build that "
                    + build + "validates by default");
        }
    }
}
