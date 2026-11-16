#include <cstdint>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/sceneholds.hpp>
#include <apps/rtxtool/model/restreuse.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/vfs/pathutil.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;

        /// **A texture woken is counted by the least cache that still held it**: its own bytes and those that came to
        /// rest after it and still rest. Three files of 40, 30 and 20 MiB rest in that order; the first woken needs a
        /// cache of 40 + 30 + 20 = 90 MiB, the third, rested last, of its own 20. A cache of 64 MiB keeps the third's
        /// 20 alone, one of 96 both, 60 MiB; one of 32 keeps the third, whose 20 falls in the first step.
        TEST(RtxRestReuseTest, aTextureWokenIsCountedByTheLeastCacheThatHeldIt)
        {
            Rtx::SceneDesc scene;
            Rtx::TextureTable& textures = scene.textures();
            const auto rest = [&](const char* path, const std::uint64_t mebibytes) {
                const Rtx::Index slot = textures.add(VFS::Path::NormalizedView(path));
                textures.stand(slot, mebibytes * sMiB, false);
                scene.drop(scene.holdTexture(slot));
                scene.compact();
                return slot;
            };
            rest("textures/a.dds", 40);
            rest("textures/b.dds", 30);
            rest("textures/c.dds", 20);

            RestReuse reuse;
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 0u) << "nothing woke";

            textures.add(VFS::Path::NormalizedView("textures/a.dds"));
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 40 * sMiB);
            EXPECT_EQ(reuse.keptUnder(64 * sMiB), 0u);
            EXPECT_EQ(reuse.keptUnder(96 * sMiB), 40 * sMiB);

            textures.add(VFS::Path::NormalizedView("textures/c.dds"));
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 60 * sMiB);
            EXPECT_EQ(reuse.keptUnder(32 * sMiB), 20 * sMiB);
            EXPECT_EQ(reuse.keptUnder(64 * sMiB), 20 * sMiB);
            EXPECT_EQ(reuse.keptUnder(96 * sMiB), 60 * sMiB);

            // A texture freed for room is no wake, nor is one that still rests.
            scene.evictResting(30 * sMiB);
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 60 * sMiB);
            EXPECT_NE(
                reuse.describe().find("rest reuse: 60.0 MiB woke from rest; what a cache of each size kept of it: "
                                      "64 MiB 33.3%, 128 MiB 100.0%,"),
                std::string::npos)
                << reuse.describe();

            // **A mesh in the same order**: a quad whose structure takes 10 MiB rests, nothing resting after it, and
            // wakes needing only its own bytes.
            const Rtx::Index quad = Rtx::Testing::addQuadMesh(scene);
            scene.standMesh(quad, 10 * sMiB);
            const std::uint64_t quadBytes = scene.meshes().getRows()[quad].mStoodBytes;
            Rtx::Testing::letGoMesh(scene, quad);
            scene.compact();
            reuse.read(scene);
            EXPECT_EQ(Rtx::Testing::addQuadMesh(scene), quad);
            reuse.read(scene);
            EXPECT_EQ(reuse.getWoken(), 60 * sMiB + quadBytes);
            EXPECT_EQ(reuse.keptUnder(32 * sMiB), 20 * sMiB + quadBytes);
        }
    }
}
