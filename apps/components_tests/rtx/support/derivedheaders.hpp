#pragma once

#include <osg/Image>

#include <components/resource/imageheader.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/preprocess/imagederivation.hpp>

namespace Rtx::Testing
{
    /// A test's image manager handing models headers, each derived as the renderer derives it (`ImageDerivation`),
    /// with no store. Declared after the manager, so the derivation is taken back before the manager goes.
    class DerivedHeaders
    {
    public:
        explicit DerivedHeaders(Resource::ImageManager& images)
            : mImages(images)
        {
            images.setModelImages(Resource::ModelImages::Headers,
                [this](const osg::Image& texels) { return mDerivation.derive(texels); });
        }

        DerivedHeaders(const DerivedHeaders&) = delete;
        DerivedHeaders& operator=(const DerivedHeaders&) = delete;

        ~DerivedHeaders() { mImages.setModelImages(Resource::ModelImages::Texels, {}); }

    private:
        Resource::ImageManager& mImages;
        ImageDerivation mDerivation{ nullptr, 1 };
    };
}
