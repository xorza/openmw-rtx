#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include <osg/CopyOp>
#include <osg/GL>
#include <osg/Image>
#include <osg/Object>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

namespace Resource
{
    class ImageManager;

    /// What a header holds of its file's image: its size, its format and its levels' layout, with no texel.
    struct HeaderShape
    {
        std::int32_t mWidth = 0;
        std::int32_t mHeight = 0;
        std::int32_t mDepth = 0;
        GLint mInternalFormat = 0;
        GLenum mPixelFormat = 0;
        GLenum mDataType = 0;
        std::uint32_t mPacking = 1;
        std::int32_t mRowLength = 0;
        osg::Image::Origin mOrigin = osg::Image::BOTTOM_LEFT;
        osg::Image::MipmapDataType mMipmaps;

        bool operator==(const HeaderShape& other) const = default;
    };

    /// `image`'s shape, as a header of it holds it.
    HeaderShape shapeOf(const osg::Image& image);

    /// **A file's image without its texels**: its name, its size, its format and its levels' layout, for a model whose
    /// renderer reads a texture's texels itself and keeps them only until the device has its copy (`ModelImages`,
    /// `docs/architecture.md`, "A model holds a texture's header, not its texels"). The texels are its manager's to
    /// read again (`texelsOf`), the handle's server as an asset handle names it. Asked of only while its manager
    /// lives, which the resource system that owns both sees to.
    class ImageHeader final : public osg::Image
    {
    public:
        /// The header of the file `fileName`, of `shape`, whose texels `source` reads again, carrying what its
        /// manager's deriver made of them (`HeaderDeriver`).
        ImageHeader(const HeaderShape& shape, const std::string& fileName, ImageManager& source,
            osg::ref_ptr<const osg::Referenced> derived);

        ImageHeader() = default;

        /// The copy `META_Object`'s `clone` makes, as OpenSceneGraph copies every object; not assigned, as no
        /// referenced object is.
        ImageHeader(const ImageHeader& copy, const osg::CopyOp& op = osg::CopyOp::SHALLOW_COPY);
        ImageHeader& operator=(const ImageHeader&) = delete;

        META_Object(Resource, ImageHeader)

        /// The file's image with its texels: its manager's, from its cache or read again.
        osg::ref_ptr<const osg::Image> readTexels() const;

        /// Tells the manager the caller is done with the texels: its cache lets go of them, and what still holds
        /// them keeps them.
        void releaseTexels() const;

        /// What a reader derived from the file's texels, kept with the header so every thread that meets the file
        /// finds it rather than reading the texels again once they are let go of. **Made before the header was, and
        /// never changed**: any thread reads it without a lock.
        template <class Derived>
        const Derived& derived() const
        {
            assert(mDerived != nullptr && "a header made with nothing derived");
            assert(dynamic_cast<const Derived*>(mDerived.get()) != nullptr
                && "a header asked for another kind than it carries");
            return static_cast<const Derived&>(*mDerived);
        }

    protected:
        ~ImageHeader() override = default;

    private:
        ImageManager* mSource = nullptr;
        osg::ref_ptr<const osg::Referenced> mDerived;
    };

    /// A header a deriver knew with no read of its file: its shape, and what was derived of it.
    struct HeaderFound
    {
        HeaderShape mShape;
        osg::ref_ptr<const osg::Referenced> mDerived;
    };

    /// What a deriver answers of a file before it is read: the header it knows, or none; and the file's stamp as it
    /// stood before the read, which a derivation is filed under (`HeaderDeriver::derive`).
    struct HeaderLookup
    {
        std::optional<HeaderFound> mFound;
        std::uint64_t mStamp = 0;
    };

    /// **What a renderer derives of a file where its texels are first read** (`ImageManager::setModelImages`): what it
    /// keys and learns of the file, which the header is made carrying (`ImageHeader::derived`), so nothing reads the
    /// file again for it, as OpenSceneGraph hooks every read through its registry's `ReadFileCallback`. **Asked first
    /// for what it knows of the file unread**, as Git's index answers a file whose stat stands, so a session whose
    /// files stood as they were reads none of them for a header. Asked from every loader's thread at once.
    class HeaderDeriver
    {
    public:
        /// A file read after `find` answered it unknown: its path, its stamp as `find` answered it, its header's
        /// shape and its texels.
        struct Read
        {
            VFS::Path::NormalizedView mPath;
            std::uint64_t mStamp = 0;
            const HeaderShape& mShape;
            const osg::Image& mTexels;
        };

        virtual ~HeaderDeriver() = default;

        /// What is known of `path` with no read of it.
        virtual HeaderLookup find(VFS::Path::NormalizedView path) = 0;

        /// What `read`'s texels come to.
        virtual osg::ref_ptr<const osg::Referenced> derive(const Read& read) = 0;
    };

    /// Whether `image` is a header (`ImageHeader`), whose texels its manager reads: asked without reading them.
    bool isHeader(const osg::Image& image);

    /// `image`'s texels: `image` itself where it holds them, and its manager's where it is a header (`ImageHeader`),
    /// which may read the file. The one way to the texels of a model's texture.
    osg::ref_ptr<const osg::Image> texelsOf(const osg::Image& image);

    /// Tells the manager of a header the caller is done with its texels (`ImageHeader::releaseTexels`); nothing for an
    /// image that holds its own, which its holders keep.
    void releaseTexels(const osg::Image& image);
}
