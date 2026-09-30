#include "png.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>
#include <osgDB/Options>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgDB/WriteFile>
#include <zlib.h>

#include <components/crashcatcher/crash.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/common/error.hpp>

#include "frameimage.hpp"

namespace Rtx
{
    namespace
    {
        void appendBigEndian(std::string& bytes, const std::uint32_t value)
        {
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes += static_cast<char>((value >> shift) & 0xffu);
        }

        /// A PNG's `iTXt` chunk saying `text` under `keyword`: uncompressed, and no language, so the
        /// text is UTF-8 and read as written. The CRC covers the type and the data, not the length.
        std::string textChunk(const std::string_view keyword, const std::string_view text)
        {
            std::string body = "iTXt";
            body += keyword;
            body += std::string_view("\0\0\0\0\0", 5);
            body += text;

            std::string chunk;
            appendBigEndian(chunk, static_cast<std::uint32_t>(body.size() - 4));
            chunk += body;
            appendBigEndian(chunk,
                static_cast<std::uint32_t>(crc32(crc32(0L, nullptr, 0), reinterpret_cast<const Bytef*>(body.data()),
                    static_cast<uInt>(body.size()))));
            return chunk;
        }
    }

    void writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
        std::span<const std::uint8_t> pixels, const std::string_view description)
    {
        // The one conversion of a traced picture to OpenSceneGraph's rows, at its own size.
        const osg::ref_ptr<osg::Image> image
            = frameImage(TracedFrame{ .mWidth = width, .mHeight = height, .mPixels = pixels }, static_cast<int>(width),
                static_cast<int>(height), RowOrder::BottomFirst);
        Crash::contract(image != nullptr, "a picture written from fewer pixels than its size");

        // zlib's fastest level: a 1080p frame in 60 ms against 260 at the plugin's default, for a
        // file a fifth larger — and a run that keeps every frame writes hundreds of them.
        const osg::ref_ptr<osgDB::Options> options = new osgDB::Options("PNG_COMPRESSION 1");
        if (description.empty())
        {
            if (!osgDB::writeImageFile(*image, Files::pathToUnicodeString(path), options.get()))
                throw InputError("cannot write " + Files::pathToUnicodeString(path));
            return;
        }

        // **Encoded in memory and the chunk put in before the end**, because OSG's plugin writes no
        // text of its own. Before `IEND`, which is the last twelve bytes of every PNG and after
        // which a decoder reads nothing.
        osgDB::ReaderWriter* const png = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (png == nullptr)
            throw InputError("cannot write " + Files::pathToUnicodeString(path) + ": no PNG plugin");

        std::ostringstream encoded(std::ios::binary);
        if (!png->writeImage(*image, encoded, options.get()).success())
            throw InputError("cannot write " + Files::pathToUnicodeString(path));

        std::string bytes = std::move(encoded).str();
        constexpr std::size_t endChunk = 12;
        Crash::contract(bytes.size() > endChunk && bytes.compare(bytes.size() - 8, 4, "IEND") == 0,
            "the PNG plugin wrote a file that does not end in IEND");
        bytes.insert(bytes.size() - endChunk, textChunk("Description", description));

        std::ofstream file(path, std::ios::binary);
        if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
            throw InputError("cannot write " + Files::pathToUnicodeString(path));
    }

    PngImage readPng(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return PngImage{};

        const osg::ref_ptr<osg::Image> image = osgDB::readRefImageFile(Files::pathToUnicodeString(path));
        if (image == nullptr || image->s() <= 0 || image->t() <= 0 || image->getDataType() != GL_UNSIGNED_BYTE)
            return PngImage{};

        const int channels = image->getPixelFormat() == GL_RGBA ? 4 : image->getPixelFormat() == GL_RGB ? 3 : 0;
        if (channels == 0)
            return PngImage{};

        PngImage read{ static_cast<std::uint32_t>(image->s()), static_cast<std::uint32_t>(image->t()), {} };
        read.mPixels.resize(std::size_t{ read.mWidth } * read.mHeight * 4);

        // Back the way `writePng` sent them: OSG's first row is the bottom one.
        for (std::uint32_t y = 0; y < read.mHeight; ++y)
        {
            const unsigned char* row = image->data(0, static_cast<int>(read.mHeight - 1 - y));
            std::uint8_t* into = read.mPixels.data() + std::size_t{ y } * read.mWidth * 4;

            for (std::uint32_t x = 0; x < read.mWidth; ++x)
            {
                std::memcpy(into + std::size_t{ x } * 4, row + std::size_t{ x } * channels, channels);
                if (channels == 3)
                    into[std::size_t{ x } * 4 + 3] = 255;
            }
        }

        return read;
    }
}
