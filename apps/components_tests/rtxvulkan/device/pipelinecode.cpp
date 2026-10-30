#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/pipelinecode.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// **A variant's words are one segment, a second pipeline of a name is numbered, and so is a
        /// second executable.** The spaces of `landing sun moons` and the slash and colon a name could
        /// carry become `-`; `.`, `_` and `-` stay; `@` and the copy's number follow from the second
        /// copy; the executable's number only where there is more than one.
        TEST(RtxPipelineCodeTest, aPipelinesFileIsItsNameKeptToOneSegment)
        {
            const std::filesystem::path folder = "code";
            EXPECT_EQ(pipelineCodeFile(folder, "landing sun moons", 0, 0, 1), folder / "landing-sun-moons.txt");
            EXPECT_EQ(pipelineCodeFile(folder, "a/b:c d_e.f-g", 0, 0, 1), folder / "a-b-c-d_e.f-g.txt");
            EXPECT_EQ(pipelineCodeFile(folder, "shadow-filter", 2, 0, 1), folder / "shadow-filter@3.txt");
            EXPECT_EQ(pipelineCodeFile(folder, "gui premultiplied", 0, 0, 2), folder / "gui-premultiplied.0.txt");
            EXPECT_EQ(pipelineCodeFile(folder, "gui premultiplied", 1, 1, 2), folder / "gui-premultiplied@2.1.txt");
        }

        /// **Each form under its heading, a blank line between**, in the driver's order; a body that
        /// ends without a newline is given one, one that has it is not given a second, and nothing
        /// at all is nothing.
        TEST(RtxPipelineCodeTest, theFormsStandUnderTheirHeadingsInTheDriversOrder)
        {
            const std::array<PipelineCodeForm, 2> forms{ {
                { .mName = "ACO IR", .mDescription = "The ACO IR after some optimizations", .mText = "v_mov\n" },
                { .mName = "Assembly", .mDescription = "Final Assembly", .mText = "s_endpgm" },
            } };

            EXPECT_EQ(pipelineCodeText(forms),
                "## ACO IR — The ACO IR after some optimizations\n\nv_mov\n"
                "\n## Assembly — Final Assembly\n\ns_endpgm\n");
            EXPECT_EQ(pipelineCodeText({}), "");
        }

        /// **Three pipelines of one name write three files**, where the shadow filter's three fields
        /// once wrote one, the last over the first two; a name of its own starts its own count; and
        /// the folder is made where it was not.
        TEST(RtxPipelineCodeTest, pipelinesThatShareANameWriteBesideEachOther)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "code";
            std::filesystem::remove_all(folder);
            PipelineCodeFolder code(folder);
            ASSERT_TRUE(std::filesystem::is_directory(folder));

            const auto written = [&](const std::filesystem::path& name) {
                std::ifstream file(folder / name);
                return std::string(std::istreambuf_iterator<char>(file), {});
            };

            for (const std::string_view field : { "1", "2", "3" })
            {
                const std::array<PipelineCodeForm, 1> forms{
                    { { .mName = "Assembly", .mDescription = "", .mText = field } }
                };
                code.write("shadow-filter", code.claim("shadow-filter"), 0, 1, forms);
            }
            EXPECT_EQ(code.claim("shadow-mask"), 0u);

            EXPECT_EQ(written("shadow-filter.txt"), "## Assembly — \n\n1\n");
            EXPECT_EQ(written("shadow-filter@2.txt"), "## Assembly — \n\n2\n");
            EXPECT_EQ(written("shadow-filter@3.txt"), "## Assembly — \n\n3\n");
        }
    }
}
