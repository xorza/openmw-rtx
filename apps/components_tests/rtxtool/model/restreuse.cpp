#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/sceneholds.hpp>
#include <apps/rtxtool/model/restreuse.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;

        /// **A mesh woken is counted by the least cache that still held it**: its own bytes and those that came to
        /// rest after it and still rest. Three quads whose structures take 40, 30 and 20 MiB rest in that order; the
        /// first woken needs a cache of 40 + 30 + 20 = 90 MiB, the third, rested last, of its own 20. A cache of 64 MiB
        /// keeps the third's 20 alone, one of 96 both, 60 MiB; one of 32 keeps the third, whose 20 falls in the first
        /// step. One freed for room is no wake, nor is one that still rests.
        TEST(RtxRestReuseTest, aMeshWokenIsCountedByTheLeastCacheThatHeldIt)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            const std::array<osg::Vec3f, 4> a = Rtx::Testing::unitQuadAt(0.0f);
            const std::array<osg::Vec3f, 4> b = Rtx::Testing::unitQuadAt(1.0f);
            const std::array<osg::Vec3f, 4> c = Rtx::Testing::unitQuadAt(2.0f);
            const auto rest = [&](const std::array<osg::Vec3f, 4>& corners, const std::uint64_t mebibytes) {
                const Rtx::MeshIndex mesh = Rtx::Testing::addQuadMesh(scene, corners);
                scene.standMesh(mesh, mebibytes * sMiB);
                scene.clearArrivals();
                Rtx::Testing::letGoMesh(scene, mesh);
                scene.compact();
                return mesh;
            };
            rest(a, 40);
            rest(b, 30);
            rest(c, 20);

            RestReuse reuse;
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 0u) << "nothing woke";

            std::ignore = Rtx::Testing::addQuadMesh(scene, a);
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 40 * sMiB);
            EXPECT_EQ(reuse.keptUnder(64 * sMiB), 0u);
            EXPECT_EQ(reuse.keptUnder(96 * sMiB), 40 * sMiB);

            std::ignore = Rtx::Testing::addQuadMesh(scene, c);
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 60 * sMiB);
            EXPECT_EQ(reuse.keptUnder(32 * sMiB), 20 * sMiB);
            EXPECT_EQ(reuse.keptUnder(64 * sMiB), 20 * sMiB);
            EXPECT_EQ(reuse.keptUnder(96 * sMiB), 60 * sMiB);

            scene.evictResting(30 * sMiB);
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 60 * sMiB);
            EXPECT_NE(
                reuse.describe().find("rest reuse: 60.0 MiB woke from rest; what a cache of each size kept of it: "
                                      "64 MiB 33.3%, 128 MiB 100.0%,"),
                std::string::npos)
                << reuse.describe();
        }
    }
}
