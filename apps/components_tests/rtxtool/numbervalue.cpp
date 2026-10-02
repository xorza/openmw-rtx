#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/program_options/errors.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/rtxtool/numbervalue.hpp>

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// `--x` read through `semantic` off one line, or the refusal Boost reports.
        template <class T>
        std::string readOne(bpo::value_semantic* semantic, const std::string& argument, T& into)
        {
            bpo::options_description options;
            options.add_options()("x", semantic, "");
            const std::vector<std::string> line{ "--x=" + argument };
            try
            {
                bpo::variables_map variables;
                bpo::store(bpo::command_line_parser(line).options(options).run(), variables);
                into = variables["x"].as<T>();
                return "";
            }
            catch (const bpo::error& refused)
            {
                return refused.what();
            }
        }

        /// **One rule for a number the line gives**: the whole text, a finite number, inside the
        /// range the option states — and the help prints that range where Boost prints `arg`.
        /// Boost's own reader took `nan` and `inf`, and `--delight=5` passed a range the help stated.
        TEST(RtxNumberValueTest, aNumberIsTheWholeTextFiniteAndInsideItsRange)
        {
            float read = -1.0f;
            EXPECT_EQ(readOne(number(between(0.0f, 1.0f)), "0.25", read), "");
            EXPECT_EQ(read, 0.25f);
            EXPECT_EQ(readOne(number(between(0.0f, 1.0f)), "1", read), "") << "a closed end is in";

            for (const char* refused : { "nan", "inf", "5", "-0.5", "0.5x", " 0.5", "+0.5", "" })
                EXPECT_NE(readOne(number(between(0.0f, 1.0f)), refused, read), "") << refused;
            EXPECT_EQ(readOne(number(between(0.0f, 1.0f)), "5", read),
                "the argument ('5') for option '--x' is not a number 0..1");

            EXPECT_NE(readOne(number(atLeast(0.0f, true)), "0", read), "") << "an open end is out";
            EXPECT_NE(readOne(number(between(0.0f, 24.0f, true)), "24", read), "");

            std::uint32_t count = 0;
            EXPECT_EQ(readOne(number(anyNumber<std::uint32_t>()), "12", count), "");
            EXPECT_EQ(count, 12u);
            EXPECT_NE(readOne(number(anyNumber<std::uint32_t>()), "-1", count), "");
            EXPECT_NE(readOne(number(anyNumber<std::uint32_t>()), "1.5", count), "");

            EXPECT_EQ(number(between(0.0f, 1.0f))->name(), "0..1");
            EXPECT_EQ(number(atLeast(0.0f, true))->name(), ">0");
            EXPECT_EQ(number(atLeast(0.0f))->name(), ">=0");
            EXPECT_EQ(number(between(0.0f, 24.0f, true))->name(), "0..<24");
            EXPECT_EQ(number(anyNumber<float>())->name(), "number");
            EXPECT_EQ(number(between(0.0f, 1.0f))->default_value(0.5f)->name(), "0..1 (=0.5)")
                << "and the default beside it, as Boost prints one";
        }
    }
}
