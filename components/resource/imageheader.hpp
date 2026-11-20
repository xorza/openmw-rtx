#pragma once

#include <cassert>
#include <functional>

#include <osg/CopyOp>
#include <osg/Image>
#include <osg/Object>
#include <osg/Referenced>
#include <osg/ref_ptr>

namespace Resource
{
    class ImageManager;

    /// **A file's image without its texels**: its name, its size, its format and its levels' layout, for a model whose
    /// renderer reads a texture's texels itself and keeps them only until the device has its copy (`ModelImages`,
    /// `docs/architecture.md`, "A model holds a texture's header, not its texels"). The texels are its manager's to
    /// read again (`texelsOf`), the handle's server as an asset handle names it. Asked of only while its manager
    /// lives, which the resource system that owns both sees to.
    class ImageHeader final : public osg::Image
    {
    public:
        /// The header of `pixels`, a file's image as its manager read it, whose texels `source` reads again, carrying
        /// what its manager's deriver made of them (`HeaderDeriver`), or null where there is no deriver.
        ImageHeader(const osg::Image& pixels, ImageManager& source, osg::ref_ptr<const osg::Referenced> derived);

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

    /// **What a renderer derives of a file where its texels are first read**, handed the texels on the thread that
    /// read them, before the header exists (`ImageManager::setModelImages`): what it keys and learns of the file, which
    /// the header is made carrying (`ImageHeader::derived`), so nothing reads the file again for it. As OpenSceneGraph
    /// hooks every read through its registry's `ReadFileCallback`.
    using HeaderDeriver = std::function<osg::ref_ptr<const osg::Referenced>(const osg::Image& texels)>;

    /// Whether `image` is a header (`ImageHeader`), whose texels its manager reads: asked without reading them.
    bool isHeader(const osg::Image& image);

    /// `image`'s texels: `image` itself where it holds them, and its manager's where it is a header (`ImageHeader`),
    /// which may read the file. The one way to the texels of a model's texture.
    osg::ref_ptr<const osg::Image> texelsOf(const osg::Image& image);

    /// Tells the manager of a header the caller is done with its texels (`ImageHeader::releaseTexels`); nothing for an
    /// image that holds its own, which its holders keep.
    void releaseTexels(const osg::Image& image);
}
