#include <gtest/gtest.h>

#include <components/version/version.hpp>

namespace Version
{
    namespace
    {
        /// **A build names the fork's release and the upstream it is based on**: a release, a commit past one, and a
        /// clone too shallow to reach the upstream merge, whose commit is left out. The commit is cut to ten digits,
        /// and the upstream stands on a line of its own.
        TEST(VersionTest, aBuildNamesTheForksReleaseAndTheUpstreamItIsBasedOn)
        {
            EXPECT_EQ(describeVersion({ .mRtx = "26.11.0",
                          .mUpstream = "0.53.0",
                          .mUpstreamCommit = "b58b6758d751ce190aa8700d57e7f58fda4742d2" }),
                "OpenMW-RTX 26.11.0\nbased on OpenMW 0.53.0 (b58b6758d7)");
            EXPECT_EQ(describeVersion({ .mRtx = "26.11.0-12-g7c9343fa4d",
                          .mUpstream = "0.53.0",
                          .mUpstreamCommit = "b58b6758d751ce190aa8700d57e7f58fda4742d2" }),
                "OpenMW-RTX 26.11.0-12-g7c9343fa4d\nbased on OpenMW 0.53.0 (b58b6758d7)");
            EXPECT_EQ(describeVersion({ .mRtx = "7c9343fa4d", .mUpstream = "0.53.0", .mUpstreamCommit = {} }),
                "OpenMW-RTX 7c9343fa4d\nbased on OpenMW 0.53.0");
        }

        /// **The documentation is upstream's latest, whatever the build**: a fork's release tag names no upstream
        /// release, and the link to one that was not published was dead.
        TEST(VersionTest, theDocumentationIsUpstreamsLatest)
        {
            EXPECT_EQ(getDocumentationUrl(), "https://openmw.readthedocs.io/en/latest/");
        }
    }
}
