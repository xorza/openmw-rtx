#pragma once

#include <cstdint>

#include <components/contentstore/key.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    struct TexelScratch;
    struct PassStats;

    /// The version a file's key is made under (`hashFile`): up whenever the key of the same texels changes, which
    /// files every pass's output of every file apart from the older ones.
    inline constexpr std::uint32_t sFileKeyVersion = 1;

    /// **The key of a file's texels, every image pass's input as its key states it** (`docs/architecture.md`, "A
    /// file is keyed once, where its texels are first read"): its format, its count of levels, and each level's
    /// extent and bytes, as a description read with no encoding's choice lays them. Not the file's name, so one
    /// picture under two names is one key; not the size OpenSceneGraph counts the image at, which differs between its
    /// builds for a block format with no chain, where it describes; as OpenSceneGraph holds it where it does not.
    ContentStore::Key hashFile(const osg::Image& texels, TexelScratch& scratch);

    /// The file keys a preprocessor's image passes ask, one a preprocessor beside the scratch it hashes through, each
    /// hash counted where its stats say (`ContentStats::mFileKeys`).
    class FileKeys
    {
    public:
        FileKeys(TexelScratch& scratch, PassStats& counted)
            : mScratch(scratch)
            , mCounted(counted)
        {
        }

        FileKeys(const FileKeys&) = delete;
        FileKeys& operator=(const FileKeys&) = delete;
        ~FileKeys() = default;

        /// `image`'s: a header's as derived where its file was first read, which reads nothing; else hashed off
        /// `image`'s own texels.
        ContentStore::Key of(const osg::Image& image);

    private:
        TexelScratch& mScratch;
        PassStats& mCounted;
    };
}
