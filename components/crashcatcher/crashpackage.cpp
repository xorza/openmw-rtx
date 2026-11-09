#include "crashpackage.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <zlib.h>

#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/wholefile.hpp>

namespace Crash
{
    namespace
    {
        /// What the central directory repeats of an entry once its data is written.
        struct Entry
        {
            const PackageFile* mFile = nullptr;
            std::uint16_t mFlags = 0;
            std::uint32_t mCrc = 0;
            std::uint32_t mCompressed = 0;
            std::uint32_t mSize = 0;
            std::uint32_t mOffset = 0;
        };

        /// 2.0, the first version with deflate: what an entry needs to be read, and what wrote it,
        /// with the host byte nought, which says nothing of Unix permissions.
        constexpr std::uint16_t sVersion = 20;
        constexpr std::uint16_t sDeflated = 8;

        /// The name is UTF-8, where the format's default is code page 437.
        constexpr std::uint16_t sUtf8Name = 1 << 11;

        /// Every size and offset of a zip without Zip64 is four bytes.
        constexpr std::uint64_t sLargest = 0xFFFFFFFFu;

        /// The longest new-issue address to hand GitHub: measured in September 2026, a body of 6000
        /// bytes opens the page, and 7500 answer 500 and 8200 answer 414.
        constexpr std::size_t sLongestUrl = 6000;

        /// Appends `text` to `into` with every byte but the unreserved ones and `kept` percent-encoded:
        /// UTF-8 as it is, byte by byte.
        void percentEncode(std::string& into, std::string_view text, std::string_view kept)
        {
            static constexpr char sHex[] = "0123456789ABCDEF";
            for (const char c : text)
            {
                const auto byte = static_cast<unsigned char>(c);
                const bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '.' || c == '_' || c == '~';
                if (unreserved || kept.find(c) != std::string_view::npos)
                    into += c;
                else
                {
                    into += '%';
                    into += sHex[byte >> 4];
                    into += sHex[byte & 0xF];
                }
            }
        }

        /// Where a local header keeps the CRC, which the sizes follow: known once the data is written.
        constexpr std::size_t sCrcAt = 14;

        void put16(std::string& into, std::uint16_t value)
        {
            into.push_back(static_cast<char>(value & 0xFF));
            into.push_back(static_cast<char>(value >> 8));
        }

        void put32(std::string& into, std::uint32_t value)
        {
            put16(into, static_cast<std::uint16_t>(value & 0xFFFF));
            put16(into, static_cast<std::uint16_t>(value >> 16));
        }

        /// The MS-DOS date and time, the time in the low half, as both headers store them in turn:
        /// seconds halved, years from 1980, and nothing outside 1980 to 2107.
        std::uint32_t dosTime(const std::tm& local)
        {
            const int year = local.tm_year + 1900;
            if (year < 1980)
                return (1u << 5 | 1u) << 16;
            if (year > 2107)
                return ((127u << 9 | 12u << 5 | 31u) << 16) | (23u << 11 | 59u << 5 | 29u);

            const auto date = static_cast<std::uint32_t>((year - 1980) << 9 | (local.tm_mon + 1) << 5 | local.tm_mday);
            // A leap second's 60 would halve to 30, which the five bits allow and no reader expects.
            const auto time
                = static_cast<std::uint32_t>(local.tm_hour << 11 | local.tm_min << 5 | std::min(local.tm_sec, 59) / 2);
            return date << 16 | time;
        }

        bool isAscii(std::string_view text)
        {
            return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        }

        class Deflater
        {
        public:
            Deflater()
            {
                // Raw deflate, the stream a zip entry holds: no zlib header, no trailer.
                mStarted = deflateInit2(&mStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY)
                    == Z_OK;
            }

            Deflater(const Deflater&) = delete;
            Deflater& operator=(const Deflater&) = delete;

            ~Deflater()
            {
                if (mStarted)
                    deflateEnd(&mStream);
            }

            z_stream mStream{};
            bool mStarted = false;
        };

        /// Deflates `file` into `archive` after its local header, and fills the rest of `entry`.
        Result<void, std::string> writeData(
            std::string& archive, const PackageFile& file, Entry& entry, Misc::ByteBuffer& in, std::vector<char>& out)
        {
            // **Read whole, to its end** (`Platform::readWhole`): what the entry holds is what the
            // file held, and a read that failed is no short entry passed as a whole one.
            if (const Result<void, Platform::IoError> read = Platform::readWhole(file.mSource, in, sLargest);
                !read.isOk())
            {
                if (read.error().mFailure == Platform::IoFailure::TooLarge)
                    return Err{ file.mName + " is 4 GiB or more, which a zip without Zip64 cannot hold" };
                return Err{ Files::pathToUnicodeString(file.mSource)
                    + " could not be read: " + read.error().mCode.message() };
            }

            Deflater deflater;
            if (!deflater.mStarted)
                return Err{ "zlib would not start a deflate stream" };
            z_stream& stream = deflater.mStream;

            const uLong crc
                = crc32(crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(in.data()), static_cast<uInt>(in.size()));

            // The whole input at once, and the output taken a buffer at a time until deflate leaves
            // room in it: it has then ended the stream.
            const std::size_t start = archive.size();
            stream.next_in = reinterpret_cast<Bytef*>(in.data());
            stream.avail_in = static_cast<uInt>(in.size());
            int result = Z_OK;
            do
            {
                stream.next_out = reinterpret_cast<Bytef*>(out.data());
                stream.avail_out = static_cast<uInt>(out.size());
                result = deflate(&stream, Z_FINISH);
                archive.append(out.data(), out.size() - stream.avail_out);
            } while (stream.avail_out == 0);

            if (result != Z_STREAM_END)
                return Err{ "zlib did not end the deflate stream of " + file.mName };
            const std::uint64_t compressed = archive.size() - start;
            if (compressed > sLargest)
                return Err{ file.mName + " deflates to 4 GiB or more, which a zip without Zip64 cannot hold" };

            entry.mCrc = static_cast<std::uint32_t>(crc);
            entry.mCompressed = static_cast<std::uint32_t>(compressed);
            entry.mSize = static_cast<std::uint32_t>(in.size());
            return {};
        }

        /// The whole package, made in memory: a crash's dump and logs are megabytes, and a package
        /// written whole is one no reader finds half made.
        Result<std::string, std::string> makeArchive(
            std::span<const PackageFile> files, std::uint32_t stamp, std::vector<Entry>& entries)
        {
            std::string archive;
            Misc::ByteBuffer in;
            std::vector<char> out(1 << 16);
            std::string header;
            for (const PackageFile& file : files)
            {
                const std::size_t offset = archive.size();
                if (offset > sLargest)
                    return Err{ std::string("the package reached 4 GiB, which a zip without Zip64 cannot hold") };

                Entry& entry = entries.emplace_back();
                entry.mFile = &file;
                entry.mFlags = isAscii(file.mName) ? 0 : sUtf8Name;
                entry.mOffset = static_cast<std::uint32_t>(offset);

                // The CRC and the sizes as noughts for now, written over once the data is written.
                put32(archive, 0x04034b50);
                put16(archive, sVersion);
                put16(archive, entry.mFlags);
                put16(archive, sDeflated);
                put32(archive, stamp);
                put32(archive, 0);
                put32(archive, 0);
                put32(archive, 0);
                put16(archive, static_cast<std::uint16_t>(file.mName.size()));
                put16(archive, 0);
                archive += file.mName;

                if (Result<void, std::string> written = writeData(archive, file, entry, in, out); !written.isOk())
                    return Err{ written.error() };

                header.clear();
                put32(header, entry.mCrc);
                put32(header, entry.mCompressed);
                put32(header, entry.mSize);
                archive.replace(offset + sCrcAt, header.size(), header);
            }

            const std::size_t directory = archive.size();
            header.clear();
            for (const Entry& entry : entries)
            {
                put32(header, 0x02014b50);
                put16(header, sVersion);
                put16(header, sVersion);
                put16(header, entry.mFlags);
                put16(header, sDeflated);
                put32(header, stamp);
                put32(header, entry.mCrc);
                put32(header, entry.mCompressed);
                put32(header, entry.mSize);
                put16(header, static_cast<std::uint16_t>(entry.mFile->mName.size()));
                put16(header, 0);
                put16(header, 0);
                put16(header, 0);
                put16(header, 0);
                put32(header, 0);
                put32(header, entry.mOffset);
                header += entry.mFile->mName;
            }
            if (static_cast<std::uint64_t>(directory) + header.size() > sLargest)
                return Err{ std::string("the package reached 4 GiB, which a zip without Zip64 cannot hold") };

            const auto directorySize = static_cast<std::uint32_t>(header.size());
            put32(header, 0x06054b50);
            put16(header, 0);
            put16(header, 0);
            put16(header, static_cast<std::uint16_t>(entries.size()));
            put16(header, static_cast<std::uint16_t>(entries.size()));
            put32(header, directorySize);
            put32(header, static_cast<std::uint32_t>(directory));
            put16(header, 0);
            archive += header;
            return archive;
        }
    }

    Result<void, std::string> writePackage(
        const std::filesystem::path& zip, std::span<const PackageFile> files, const std::tm& local)
    {
        // A zip without Zip64 counts its entries in two bytes.
        if (files.size() > 0xFFFF)
            return Err{ "a zip without Zip64 holds 65535 files at most" };

        std::vector<Entry> entries;
        entries.reserve(files.size());
        const Result<std::string, std::string> archive = makeArchive(files, dosTime(local), entries);
        if (!archive.isOk())
            return Err{ archive.error() };

        // **Synced**: a package is what a person sends, and the monitor ends after it is written.
        if (const Result<void, Platform::IoError> written
            = Platform::writeWhole(zip, archive.value(), Platform::Durability::Synced);
            !written.isOk())
            return Err{ Files::pathToUnicodeString(zip) + " could not be written: " + written.error().mCode.message() };
        return {};
    }

    namespace
    {
        /// One of an application's packages, by what its name says: `<application>-crash-` and the
        /// time `freePackagePath` writes, `YYYY-MM-DD-HHMMSS`, which sorts as it ran, then the
        /// number of a second package in the same second, one for the first.
        struct PackageName
        {
            std::string mTime;
            int mNumber = 1;
            std::filesystem::path mPath;
        };

        std::optional<PackageName> packageNameOf(const std::filesystem::path& path, std::string_view application)
        {
            const std::string name = Files::pathToUnicodeString(path.filename());
            const std::string prefix = std::string(application) + "-crash-";
            constexpr std::size_t timeLength = std::string_view("2026-09-27-143013").size();
            if (!name.starts_with(prefix) || !name.ends_with(".zip") || name.size() < prefix.size() + timeLength + 4)
                return std::nullopt;

            PackageName package{ .mTime = name.substr(prefix.size(), timeLength), .mPath = path };
            if (!std::all_of(package.mTime.begin(), package.mTime.end(),
                    [](char c) { return c == '-' || (c >= '0' && c <= '9'); }))
                return std::nullopt;

            const std::string_view rest = std::string_view(name).substr(
                prefix.size() + timeLength, name.size() - prefix.size() - timeLength - 4);
            if (rest.empty())
                return package;
            if (!rest.starts_with('-'))
                return std::nullopt;
            const std::from_chars_result read
                = std::from_chars(rest.data() + 1, rest.data() + rest.size(), package.mNumber);
            if (read.ec != std::errc() || read.ptr != rest.data() + rest.size() || package.mNumber < 2)
                return std::nullopt;
            return package;
        }

        /// Deletes the oldest of `application`'s packages in `folder` past `sKeptPackages`. A folder
        /// that cannot be listed, or a file that cannot be deleted, is left as it is: what was written
        /// stands either way. A listing cut short prunes nothing, since its oldest are not the
        /// folder's.
        void pruneOldPackages(const std::filesystem::path& folder, std::string_view application)
        {
            const std::optional<std::vector<std::filesystem::directory_entry>> listed = Platform::listFolder(folder);
            if (!listed.has_value())
                return;

            std::vector<PackageName> packages;
            for (const std::filesystem::directory_entry& entry : *listed)
                if (std::optional<PackageName> package = packageNameOf(entry.path(), application))
                    packages.push_back(std::move(*package));
            if (packages.size() <= sKeptPackages)
                return;

            std::error_code error;

            std::sort(packages.begin(), packages.end(), [](const PackageName& left, const PackageName& right) {
                return std::tie(left.mTime, left.mNumber) < std::tie(right.mTime, right.mNumber);
            });
            for (std::size_t at = 0; at + sKeptPackages < packages.size(); ++at)
                std::filesystem::remove(packages[at].mPath, error);
        }
    }

    SessionPackage writeSessionPackage(const std::filesystem::path& folder, std::string_view application,
        const std::filesystem::path& log, std::span<const std::filesystem::path> dumps, const std::tm& local)
    {
        SessionPackage package;
        if (dumps.empty())
            return package;

        try
        {
            std::vector<PackageFile> files;
            files.reserve(dumps.size() + 1);
            const auto take = [&](const std::filesystem::path& path) {
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error))
                    files.push_back({ Files::pathToUnicodeString(path.filename()), path });
                else
                    package.mMissing.push_back(path);
            };

            // A game that crashed before it set its log up has none, which is not a missing one.
            if (!log.empty())
                take(log);
            for (const std::filesystem::path& dump : dumps)
                take(dump);
            if (files.empty())
            {
                package.mFailure = "neither the log nor a dump is on disk";
                return package;
            }

            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(folder, error).lexically_normal();
            const std::filesystem::path zip = freePackagePath(error ? folder : absolute, application, local);
            if (const Result<void, std::string> written = writePackage(zip, files, local); written.isOk())
            {
                package.mZip = zip;
                pruneOldPackages(zip.parent_path(), application);
            }
            else
                package.mFailure = written.error();
        }
        catch (const std::exception& error)
        {
            package.mZip.clear();
            package.mFailure = error.what();
        }
        return package;
    }

    std::filesystem::path freePackagePath(
        const std::filesystem::path& folder, std::string_view application, const std::tm& local)
    {
        char time[32];
        std::strftime(time, sizeof(time), "%Y-%m-%d-%H%M%S", &local);
        const std::string stem = std::string(application) + "-crash-" + time;

        std::filesystem::path path = folder / Files::pathFromUnicodeString(stem + ".zip");
        // A folder that cannot be looked in has the first name free, and the write then says why not.
        std::error_code error;
        for (int i = 2; std::filesystem::exists(path, error); ++i)
            path = folder / Files::pathFromUnicodeString(stem + "-" + std::to_string(i) + ".zip");
        return path;
    }

    std::string folderUrl(const std::filesystem::path& folder)
    {
        const std::u8string path = folder.generic_u8string();
        std::string url = "file://";
        // A drive's path, "C:/...", gets the slash a POSIX one begins with.
        if (!path.starts_with(u8'/'))
            url += '/';
        percentEncode(url, { reinterpret_cast<const char*>(path.data()), path.size() }, "/:");
        return url;
    }

    std::string newIssueUrl(
        std::string_view issues, std::string_view title, std::span<const std::string> summary, std::string_view attach)
    {
        std::string url(issues);
        url += "/new?title=";
        percentEncode(url, title, {});
        url += "&body=";
        percentEncode(
            url, "<!-- Drag " + std::string(attach) + " from the folder that opened into this box. -->\n\n```\n", {});

        std::string closing;
        percentEncode(closing, "```\n", {});
        std::string cut;
        percentEncode(cut, "[the rest is in " + std::string(attach) + "]\n", {});

        // A line fits where what must follow it fits too: the closing after the last, and before it,
        // the line that says the rest was cut, which the next line may need.
        std::string line;
        for (std::size_t i = 0; i < summary.size(); ++i)
        {
            line.clear();
            percentEncode(line, summary[i] + "\n", {});
            const std::size_t after = i + 1 == summary.size() ? 0 : cut.size();
            if (url.size() + line.size() + after + closing.size() > sLongestUrl)
            {
                url += cut;
                break;
            }
            url += line;
        }
        url += closing;
        return url;
    }
}
