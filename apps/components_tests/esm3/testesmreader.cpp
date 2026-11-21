#include <gtest/gtest.h>

#include <components/esm3/esmreader.hpp>

namespace ESM
{
    namespace
    {
        TEST(Esm3ReaderTest, getStringViewShouldReadStringEndingAtEndOfFile)
        {
            const std::string data(200 * 1024, 'a');
            ESMReader reader;
            reader.openRaw(std::make_unique<std::stringstream>(data), "stream");

            EXPECT_EQ(reader.getStringView(data.size()), data);
        }

        TEST(Esm3ReaderTest, getStringViewShouldRaiseOnTooLargeStrings)
        {
            const std::string data(200 * 1024, 'a');
            ESMReader reader;
            reader.openRaw(std::make_unique<std::stringstream>(data), "stream");

            EXPECT_THROW(reader.getStringView(data.size() + 1), std::runtime_error);
        }
    }
}
