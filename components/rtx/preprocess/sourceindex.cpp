#include "sourceindex.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <osg/GL>
#include <osg/Image>

#include <components/contentstore/key.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/platform/wholefile.hpp>
#include <components/rtx/preprocess/texture/filefactspass.hpp>
#include <components/vfs/pathutil.hpp>

#include "contentcodec.hpp"
#include "filekeys.hpp"

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sMagic = 0x53585452; // "RTXS"
        constexpr std::uint32_t sFormat = 1;

        /// The most bytes an index is read at: a file is untrusted, and a large content list's hundred thousand
        /// textures stand near fifteen megabytes.
        constexpr std::size_t sMostBytes = std::size_t{ 256 } << 20;

        /// The most levels a record's shape holds offsets of: a level a halving of a side of 2^31.
        constexpr std::uint32_t sMipmapsMost = 31;

        /// What the records were made under: the file key's version and the facts', so whichever moves leaves no
        /// record standing.
        std::uint64_t signatureNow()
        {
            ContentStore::Digest digest("sources", sFormat);
            digest.add(sFileKeyVersion);
            digest.add(FileFactsPass::sVersion);
            return digest.getKey().mHash[0];
        }

        /// The digest of `bytes`, what the file's last sixteen bytes hold of everything before them.
        ContentStore::Key digestOf(std::span<const std::byte> bytes)
        {
            ContentStore::Digest digest("sources", sFormat);
            digest.add(bytes);
            return digest.getKey();
        }

        void putShape(Misc::ByteBuffer& into, const Resource::HeaderShape& shape)
        {
            ContentBytes::put(into, shape.mWidth);
            ContentBytes::put(into, shape.mHeight);
            ContentBytes::put(into, shape.mDepth);
            ContentBytes::put(into, static_cast<std::int32_t>(shape.mInternalFormat));
            ContentBytes::put(into, static_cast<std::uint32_t>(shape.mPixelFormat));
            ContentBytes::put(into, static_cast<std::uint32_t>(shape.mDataType));
            ContentBytes::put(into, shape.mPacking);
            ContentBytes::put(into, shape.mRowLength);
            ContentBytes::put(into, static_cast<std::uint8_t>(shape.mOrigin));
            ContentBytes::putRun(into, std::span<const osg::Image::MipmapDataType::value_type>(shape.mMipmaps));
        }

        bool takeShape(std::span<const std::byte>& from, Resource::HeaderShape& shape)
        {
            std::int32_t internalFormat = 0;
            std::uint32_t pixelFormat = 0;
            std::uint32_t dataType = 0;
            std::uint8_t origin = 0;
            if (!ContentBytes::take(from, shape.mWidth) || !ContentBytes::take(from, shape.mHeight)
                || !ContentBytes::take(from, shape.mDepth) || !ContentBytes::take(from, internalFormat)
                || !ContentBytes::take(from, pixelFormat) || !ContentBytes::take(from, dataType)
                || !ContentBytes::take(from, shape.mPacking) || !ContentBytes::take(from, shape.mRowLength)
                || !ContentBytes::take(from, origin) || origin > static_cast<std::uint8_t>(osg::Image::TOP_LEFT)
                || !ContentBytes::takeRun(from, shape.mMipmaps) || shape.mMipmaps.size() > sMipmapsMost)
                return false;
            shape.mInternalFormat = internalFormat;
            shape.mPixelFormat = pixelFormat;
            shape.mDataType = dataType;
            shape.mOrigin = static_cast<osg::Image::Origin>(origin);
            return true;
        }

        /// The bytes `ContentCodec<ImageFacts>` writes, which a record holds as they stand.
        constexpr std::size_t sFactsBytes = 32;
    }

    SourceIndex::SourceIndex(std::filesystem::path path)
        : mPath(std::move(path))
    {
        read();
    }

    void SourceIndex::read()
    {
        // An index that does not read whole is an empty one, whatever kept it from reading.
        Misc::ByteBuffer bytes;
        std::error_code failed;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(mPath, failed);
        if (failed || !Platform::readWhole(mPath, bytes, sMostBytes).isOk() || bytes.size() < sizeof(ContentStore::Key))
            return;

        const std::span<const std::byte> body = std::span<const std::byte>(bytes).first(bytes.size() - 16);
        std::span<const std::byte> tail = std::span<const std::byte>(bytes).last(16);
        ContentStore::Key stated;
        if (!ContentBytes::take(tail, stated.mHash[0]) || !ContentBytes::take(tail, stated.mHash[1])
            || stated != digestOf(body))
            return;

        // The layout `write` lays. **Every count is no larger than the bytes left could hold**: the file is
        // untrusted, and a count read whole would be a gigantic allocation.
        std::span<const std::byte> from = body;
        std::uint32_t magic = 0;
        std::uint32_t format = 0;
        std::uint64_t signature = 0;
        std::uint32_t paths = 0;
        if (!ContentBytes::take(from, magic) || magic != sMagic || !ContentBytes::take(from, format)
            || format != sFormat || !ContentBytes::take(from, signature) || signature != signatureNow()
            || !ContentBytes::take(from, paths) || paths > from.size())
            return;

        std::unordered_map<std::string, std::vector<Filed>, NameHash, std::equal_to<>> read;
        read.reserve(paths);
        std::vector<char> name;
        for (std::uint32_t at = 0; at < paths; ++at)
        {
            std::uint8_t records = 0;
            if (!ContentBytes::takeRun(from, name) || !ContentBytes::take(from, records)
                || records > sRecordsPerPathMost || records == 0)
                return;
            std::string path(name.begin(), name.end());
            // **A name no path of the game's spells is no index**, as the manifest refuses one.
            if (!VFS::Path::isNormalized(path) || read.contains(path))
                return;
            std::vector<Filed>& filed = read[std::move(path)];
            filed.resize(records);
            for (Filed& one : filed)
            {
                if (!ContentBytes::take(from, one.mRecord.mStamp) || !ContentBytes::take(from, one.mDay)
                    || !takeShape(from, one.mRecord.mShape)
                    || !ContentBytes::take(from, one.mRecord.mFacts.mFile.mHash[0])
                    || !ContentBytes::take(from, one.mRecord.mFacts.mFile.mHash[1]) || from.size() < sFactsBytes
                    || !ContentCodec<ImageFacts>::decode(from.first(sFactsBytes), one.mRecord.mFacts.mFacts))
                    return;
                from = from.subspan(sFactsBytes);
            }
        }
        if (!from.empty())
            return;

        mByPath = std::move(read);
        mWritten = written;
    }

    std::optional<SourceRecord> SourceIndex::find(const std::string_view path, const std::uint64_t stamp,
        const std::filesystem::file_time_type modified, const std::uint32_t day)
    {
        const std::scoped_lock lock(mMutex);
        if (!mWritten.has_value() || modified >= *mWritten)
            return std::nullopt;
        const auto known = mByPath.find(path);
        if (known == mByPath.end())
            return std::nullopt;
        for (Filed& filed : known->second)
            if (filed.mRecord.mStamp == stamp)
            {
                mChanged = mChanged || filed.mDay != day;
                filed.mDay = day;
                return filed.mRecord;
            }
        return std::nullopt;
    }

    void SourceIndex::file(const std::string_view path, const SourceRecord& record, const std::uint32_t day)
    {
        assert(VFS::Path::isNormalized(path) && "a record of a path the game would not spell");
        const std::scoped_lock lock(mMutex);
        mChanged = true;
        auto known = mByPath.find(path);
        if (known == mByPath.end())
            known = mByPath.emplace(std::string(path), std::vector<Filed>{}).first;
        std::vector<Filed>& filed = known->second;
        const auto same
            = std::ranges::find_if(filed, [&](const Filed& one) { return one.mRecord.mStamp == record.mStamp; });
        if (same != filed.end())
        {
            *same = Filed{ .mRecord = record, .mDay = day };
            return;
        }
        if (filed.size() >= sRecordsPerPathMost)
            filed.erase(std::ranges::min_element(filed, {}, &Filed::mDay));
        filed.push_back(Filed{ .mRecord = record, .mDay = day });
    }

    void SourceIndex::dropUnnamed(const std::uint32_t day)
    {
        const std::scoped_lock lock(mMutex);
        for (auto at = mByPath.begin(); at != mByPath.end();)
        {
            const std::size_t erased
                = std::erase_if(at->second, [&](const Filed& one) { return one.mDay + sUnnamedDaysMost < day; });
            mChanged = mChanged || erased > 0;
            at = at->second.empty() ? mByPath.erase(at) : std::next(at);
        }
    }

    bool SourceIndex::isChanged() const
    {
        const std::scoped_lock lock(mMutex);
        return mChanged;
    }

    bool SourceIndex::write()
    {
        const std::scoped_lock lock(mMutex);
        if (!mChanged)
            return true;

        Misc::ByteBuffer bytes;
        ContentBytes::put(bytes, sMagic);
        ContentBytes::put(bytes, sFormat);
        ContentBytes::put(bytes, signatureNow());
        ContentBytes::put(bytes, static_cast<std::uint32_t>(mByPath.size()));
        Misc::ByteBuffer facts;
        for (const auto& [path, filed] : mByPath)
        {
            ContentBytes::putRun(bytes, std::span<const char>(path));
            ContentBytes::put(bytes, static_cast<std::uint8_t>(filed.size()));
            for (const Filed& one : filed)
            {
                ContentBytes::put(bytes, one.mRecord.mStamp);
                ContentBytes::put(bytes, one.mDay);
                putShape(bytes, one.mRecord.mShape);
                ContentBytes::put(bytes, one.mRecord.mFacts.mFile.mHash[0]);
                ContentBytes::put(bytes, one.mRecord.mFacts.mFile.mHash[1]);
                facts.clear();
                ContentCodec<ImageFacts>::encode(one.mRecord.mFacts.mFacts, facts);
                assert(facts.size() == sFactsBytes && "the facts' codec writes another length than a record holds");
                bytes.insert(bytes.end(), facts.begin(), facts.end());
            }
        }
        const ContentStore::Key digest = digestOf(bytes);
        ContentBytes::put(bytes, digest.mHash[0]);
        ContentBytes::put(bytes, digest.mHash[1]);

        if (!Platform::writeWhole(mPath, std::span<const std::byte>(bytes), Platform::Durability::Whole).isOk())
            return false;
        mChanged = false;
        return true;
    }

    std::size_t SourceIndex::size() const
    {
        const std::scoped_lock lock(mMutex);
        std::size_t records = 0;
        for (const auto& [path, filed] : mByPath)
            records += filed.size();
        return records;
    }
}
