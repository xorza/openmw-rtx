#pragma once

#include <osg/Image>

#include <components/resource/imageheader.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/preprocess/imagederivation.hpp>

namespace Rtx::Testing
{
    /// A test's image manager handing models headers, each derived as the renderer derives it (`ImageDerivation`),
    /// with no store and no record of a file. Declared after the manager, so the derivation is taken back before the
    /// manager goes.
    class DerivedHeaders final : public Resource::HeaderDeriver
    {
    public:
        explicit DerivedHeaders(Resource::ImageManager& images)
            : mImages(images)
        {
            images.setModelImages(Resource::ModelImages::Headers, this);
        }

        DerivedHeaders(const DerivedHeaders&) = delete;
        DerivedHeaders& operator=(const DerivedHeaders&) = delete;

        ~DerivedHeaders() override { mImages.setModelImages(Resource::ModelImages::Texels, nullptr); }

        Resource::HeaderLookup find(VFS::Path::NormalizedView) override { return Resource::HeaderLookup{}; }

        osg::ref_ptr<const osg::Referenced> derive(const Read& read) override
        {
            return mDerivation.derive(read.mTexels);
        }

    private:
        Resource::ImageManager& mImages;
        ImageDerivation mDerivation{ nullptr, 1 };
    };
}
