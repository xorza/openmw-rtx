#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Node>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/rtx/mirror/released.hpp>

namespace Rtx
{
    namespace
    {
        /// **A take hands over what was kept in a buffer of its own size**, and the sink keeps the
        /// room of the busiest frame: a frame that lets go of one object after one that let go of a
        /// hundred hands over one slot, and the sink grows no further. Every object reaches the
        /// buffer once, and the sink holds none of them after.
        TEST(RtxReleasedTest, aTakeHandsOverWhatWasKeptAndTheSinkKeepsItsRoom)
        {
            Released released;
            EXPECT_TRUE(released.take().empty());

            std::vector<osg::ref_ptr<osg::Node>> nodes;
            for (int at = 0; at < 100; ++at)
                released.keep(nodes.emplace_back(new osg::Node));
            const std::vector<osg::ref_ptr<const osg::Referenced>> busy = released.take();
            EXPECT_EQ(busy.size(), 100u);
            EXPECT_EQ(busy.capacity(), 100u);
            EXPECT_TRUE(released.isEmpty());
            for (std::size_t at = 0; at < nodes.size(); ++at)
                EXPECT_EQ(busy[at].get(), nodes[at].get());
            EXPECT_EQ(nodes.front()->referenceCount(), 2) << "the node and the buffer, and the sink holds none";

            released.keep(nodes.front());
            const std::vector<osg::ref_ptr<const osg::Referenced>> quiet = released.take();
            EXPECT_EQ(quiet.size(), 1u);
            EXPECT_EQ(quiet.capacity(), 1u) << "a frame that let go of one handed over the busiest frame's room";
        }
    }
}
