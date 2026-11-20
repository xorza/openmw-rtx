#include <gtest/gtest.h>

#include <components/files/constrainedfilestream.hpp>
#include <components/files/constrainedfilestreambuf.hpp>
#include <components/files/istreamptr.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **A read the system fails turns the stream bad**: the positioned read answers its failure, and the stream's
    /// buffer throws it, which a stream holds as its bad bit for the parser reading it. A folder opens for reading
    /// on Linux and reads as `EISDIR`.
    TEST(FilesConstrainedFileStreamLinuxTest, aReadTheSystemFailsTurnsTheStreamBad)
    {
        const Files::IStreamPtr stream
            = Files::openConstrainedFileStream(Files::openSharedFile(TestingOpenMW::currentTestDirPath()), 0, 16);
        char byte = 0;
        stream->read(&byte, 1);
        EXPECT_TRUE(stream->bad());
        EXPECT_EQ(stream->gcount(), 0);
    }
}
