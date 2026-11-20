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

    /// The version a file's key is made under (`hashFile`): up whenever the key of the same texels changes, which
    /// files every pass's output of every file apart from the older ones.
    inline constexpr std::uint32_t sFileKeyVersion = 1;

    /// **The key of a file's texels, every image pass's input as its key states it** (`docs/architecture.md`, "A
    /// file is keyed once, where its texels are first read"): its format, its count of levels, and each level's
    /// extent and bytes, as a description read with no encoding's choice lays them. Not the file's name, so one
    /// picture under two names is one key; not the size OpenSceneGraph counts the image at, which differs between its
    /// builds for a block format with no chain, where it describes; as OpenSceneGraph holds it where it does not.
    ContentStore::Key hashFile(const osg::Image& texels, TexelScratch& scratch);

    /// The file keys a preprocessor's image passes ask, one a preprocessor beside the scratch it hashes through.
    class FileKeys
    {
    public:
        explicit FileKeys(TexelScratch& scratch)
            : mScratch(scratch)
        {
        }

        FileKeys(const FileKeys&) = delete;
        FileKeys& operator=(const FileKeys&) = delete;
        ~FileKeys() = default;

        /// `image`'s: a header's as derived where its file was first read, which reads nothing; the key `name` was
        /// handed for the image it named; else hashed off `image`'s own texels.
        ContentStore::Key of(const osg::Image& image);

        /// Answers `key` for `texels` until `forget`: the derivation's, which keys a file once for every fact it
        /// makes of it.
        void name(const osg::Image& texels, const ContentStore::Key& key);
        void forget();

    private:
        TexelScratch& mScratch;
        const osg::Image* mNamed = nullptr;
        ContentStore::Key mNamedKey;
    };
}
