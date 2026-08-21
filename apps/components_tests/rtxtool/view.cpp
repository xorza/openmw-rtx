#include <cstddef>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/renderer.hpp>

#include <apps/rtxtool/placement.hpp>
#include <apps/rtxtool/view.hpp>

namespace RtxTool
{
    namespace
    {
        ViewRequest makeRequest()
        {
            ViewRequest request;
            request.mCell = "Balmora, Guild of Fighters";
            request.mFieldOfView = 60.0f;
            request.mWeather = "Ashstorm";
            request.mHour = 17.25f;
            request.mFilter = false;
            return request;
        }

        /// A profiling line has to be pasteable and it has to be exact, so both are asserted.
        ///
        /// The position is deliberately one that rounding would lose, and it survives because the
        /// formatting is shortest-round-trip rather than fixed. The hour avoids sunrise and sunset
        /// for the reason `.notes/ISSUES.md` gives, so that this line is one that actually runs.
        TEST(RtxProfileLineTest, everyConditionTheFrameDependsOnIsInTheLine)
        {
            const ViewRequest request = makeRequest();
            const Rtx::ValidationOptions validation{
                .mEnabled = true, .mSynchronization = true, .mGpuAssisted = false
            };
            const osg::Vec3f origin(-19216.5f, -14896.25f, 160.0f);
            const osg::Vec3f target(-19323.0f, -13903.0f, 109.5f);

            EXPECT_EQ(describeProfile(request, validation, origin, target, 2560, 1440),
                "--cell=\"Balmora, Guild of Fighters\" --pos=-19216.5,-14896.25,160 --look=-19323,-13903,109.5"
                " --fov=60 --size=2560x1440 --weather=Ashstorm --hour=17.25 --filter=false --validation=true"
                " --sync-validation=true --gpu-validation=false");
        }

        /// What the line is for: the tool's own parser reading it back to the same floats.
        TEST(RtxProfileLineTest, thePositionItPrintsIsThePositionItParsesBack)
        {
            const osg::Vec3f origin(-19216.5f, -14896.25f, 160.0f);
            const std::string line
                = describeProfile(makeRequest(), Rtx::ValidationOptions{}, origin, osg::Vec3f(1.0f, 2.0f, 3.0f), 8, 8);

            const std::size_t start = line.find("--pos=") + 6;
            const std::string printed = line.substr(start, line.find(' ', start) - start);

            const std::optional<osg::Vec3f> parsed = parseVec3(printed, "--pos");
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(*parsed, origin);
        }

        /// Each of the fields that is a flag rather than a value, since a line that dropped one
        /// would reproduce a different frame — or the same frame at a different price — while
        /// looking correct.
        TEST(RtxProfileLineTest, theAlbedoViewAndTheLayersEachChangeTheLine)
        {
            ViewRequest shaded = makeRequest();
            ViewRequest albedo = makeRequest();
            albedo.mShowAlbedo = true;

            const osg::Vec3f at(0.0f, 0.0f, 0.0f);
            const osg::Vec3f to(0.0f, 1.0f, 0.0f);
            const Rtx::ValidationOptions off{};
            const Rtx::ValidationOptions gpu{ .mEnabled = true, .mGpuAssisted = true };

            ViewRequest denoised = makeRequest();
            denoised.mFilter = true;

            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(albedo, off, at, to, 8, 8));
            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(denoised, off, at, to, 8, 8));
            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(shaded, gpu, at, to, 8, 8));
            EXPECT_TRUE(describeProfile(albedo, off, at, to, 8, 8).ends_with(" --albedo"));
        }
    }
}
