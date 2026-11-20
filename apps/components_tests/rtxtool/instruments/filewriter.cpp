#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/filewriter.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        std::string textOf(const std::filesystem::path& path)
        {
            std::ostringstream read;
            read << std::ifstream(path, std::ios::binary).rdbuf();
            return read.str();
        }

        /// **What is handed over is written whole or added at the end, in the order it was handed**: a record begun
        /// with its header and added to a line a leg, more lines than wait at once among them; a file written whole
        /// again stands as the last text alone; and a write that fails is answered once, by its file, while the
        /// writes after it go on.
        TEST(RtxFileWriterTest, whatIsHandedOverIsWrittenInItsOrderAndAFailureIsAnsweredOnce)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "file-writer";
            std::filesystem::remove_all(folder);
            std::filesystem::create_directories(folder);
            const std::filesystem::path record = folder / "record.jsonl";
            const std::filesystem::path findings = folder / "findings.txt";
            const std::filesystem::path nowhere = folder / "no-such-folder" / "lost.txt";

            std::string expected = "header\n";
            {
                FileWriter writer;
                writer.write(record, "header\n", FileWriter::As::Whole);
                for (std::size_t leg = 0; leg < 2 * FileWriter::sPendingMost + 3; ++leg)
                {
                    const std::string line = "leg " + std::to_string(leg) + "\n";
                    expected += line;
                    writer.write(record, line, FileWriter::As::Appended);
                    writer.write(findings, "after leg " + std::to_string(leg), FileWriter::As::Whole);
                }
                writer.write(nowhere, "lost", FileWriter::As::Whole);
                writer.write(nowhere, "lost", FileWriter::As::Appended);
                writer.write(findings, "the last", FileWriter::As::Whole);

                const std::vector<std::string> failed = writer.finish();
                ASSERT_EQ(failed.size(), 2u);
                EXPECT_NE(failed[0].find("lost.txt"), std::string::npos) << failed[0];
                EXPECT_NE(failed[1].find("could not add to"), std::string::npos) << failed[1];
                EXPECT_TRUE(writer.finish().empty()) << "a failure answered twice";
                EXPECT_EQ(textOf(record), expected);
                EXPECT_EQ(textOf(findings), "the last");

                // What is handed over and never waited for is written as the writer goes.
                writer.write(record, "the end\n", FileWriter::As::Appended);
                expected += "the end\n";
            }
            EXPECT_EQ(textOf(record), expected);
            EXPECT_FALSE(std::filesystem::exists(nowhere));
        }
    }
}
