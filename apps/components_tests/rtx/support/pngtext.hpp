#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <zlib.h>

namespace Rtx::Testing
{
    struct PngText
    {
        std::string mKey;
        std::string mText;
    };

    inline std::uint32_t pngWordAt(const std::string& bytes, const std::size_t at)
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i)
            value = (value << 8) | static_cast<unsigned char>(bytes[at + i]);
        return value;
    }

    inline std::uint32_t crcOf(const std::string_view bytes)
    {
        return static_cast<std::uint32_t>(crc32(
            crc32(0L, nullptr, 0), reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uInt>(bytes.size())));
    }

    /// One chunk of a PNG: its type and its data.
    struct PngChunk
    {
        std::string mType;
        std::string mData;
    };

    /// Every chunk of the PNG at `path`, walked chunk by chunk from the signature to `IEND` by the
    /// format's own framing, not by the writer's code. A chunk whose CRC does not match its type and
    /// data fails the test.
    inline std::vector<PngChunk> readPngChunks(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        std::vector<PngChunk> chunks;
        EXPECT_EQ(bytes.compare(0, 8, "\x89PNG\r\n\x1a\n"), 0) << "not a PNG";
        std::size_t at = 8;
        while (at + 12 <= bytes.size())
        {
            const std::uint32_t length = pngWordAt(bytes, at);
            const std::string_view typed = std::string_view(bytes).substr(at + 4, 4 + std::size_t{ length });
            EXPECT_EQ(pngWordAt(bytes, at + 8 + length), crcOf(typed)) << typed.substr(0, 4);
            at += 12 + std::size_t{ length };
            chunks.push_back(PngChunk{ std::string(typed.substr(0, 4)), std::string(typed.substr(4)) });

            if (typed.starts_with("IEND"))
            {
                EXPECT_EQ(at, bytes.size()) << "bytes after IEND";
                return chunks;
            }
        }

        ADD_FAILURE() << "no IEND";
        return chunks;
    }

    /// Every uncompressed `iTXt` chunk of the PNG at `path`.
    inline std::vector<PngText> readPngTexts(const std::filesystem::path& path)
    {
        std::vector<PngText> texts;
        for (const PngChunk& chunk : readPngChunks(path))
        {
            if (chunk.mType != "iTXt")
                continue;

            // keyword, 0, compression flag, method, language, 0, translated keyword, 0, text.
            const std::string_view data = chunk.mData;
            const std::size_t keyEnd = data.find('\0');
            const std::size_t languageEnd = data.find('\0', keyEnd + 3);
            const std::size_t translatedEnd = data.find('\0', languageEnd + 1);
            EXPECT_EQ(data[keyEnd + 1], '\0') << "a compressed text";
            texts.push_back(
                PngText{ std::string(data.substr(0, keyEnd)), std::string(data.substr(translatedEnd + 1)) });
        }
        return texts;
    }

    /// The image data of the PNG at `path` as the file stores it: its `IDAT` chunks joined and
    /// inflated, each row its filter byte and its samples, `bytes` long.
    inline std::string readPngRows(const std::filesystem::path& path, std::size_t bytes)
    {
        std::string deflated;
        for (const PngChunk& chunk : readPngChunks(path))
            if (chunk.mType == "IDAT")
                deflated += chunk.mData;

        std::string rows(bytes, '\0');
        uLongf inflated = static_cast<uLongf>(bytes);
        EXPECT_EQ(uncompress(reinterpret_cast<Bytef*>(rows.data()), &inflated,
                      reinterpret_cast<const Bytef*>(deflated.data()), static_cast<uLong>(deflated.size())),
            Z_OK);
        EXPECT_EQ(inflated, bytes) << "the image data is another length";
        return rows;
    }
}
