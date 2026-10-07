#include "tracedoverlay.hpp"

#include <cassert>
#include <cstring>
#include <utility>

#include <osg/GL>
#include <osg/Image>

#include <apps/openmw/mwrender/offscreenview.hpp>
#include <apps/openmw/mwrender/pixels.hpp>
#include <components/myguirtx/rendermanager.hpp>
#include <components/sceneutil/paintedtexture.hpp>

#include "viewqueue.hpp"

namespace MWRender
{
    TracedOverlay::TracedOverlay(const MapOverlaySpec& spec, ViewQueue& views, MyGUIRtx::RenderManager& gui)
        : mViews(views)
        , mLandAlpha(spec.mLandAlpha)
    {
        mImage = new osg::Image;
        mImage->allocateImage(spec.mWidth, spec.mHeight, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        assert(mImage->isDataContiguous());
        std::memset(mImage->data(), 0, mImage->getTotalSizeInBytes());

        mTexture = new SceneUtil::PaintedTexture(mImage);
        mGuiTexture = gui.shareTexture(*mTexture);

        mViews.adoptOverlay(*this);
    }

    TracedOverlay::~TracedOverlay()
    {
        mViews.forgetOverlay(*this);
    }

    void TracedOverlay::paintTile(const SceneUtil::ImageRegion& destination, std::shared_ptr<OffscreenView> tile)
    {
        mPending.add(destination, std::move(tile));
    }

    void TracedOverlay::finish()
    {
        mPending.finish([this](const SceneUtil::ImageRegion& destination, const osg::Image& drawn) {
            composite(destination, drawn);
        });
    }

    void TracedOverlay::composite(const SceneUtil::ImageRegion& destination, const osg::Image& tile)
    {
        if (compositeTile(tile, *mLandAlpha, *mImage, destination, mCellScratch))
            mTexture->paint(destination);
    }

    void TracedOverlay::paintImage(
        const SceneUtil::ImageRegion& destination, osg::ref_ptr<osg::Image> image, const SceneUtil::ImageRegion& source)
    {
        mPending.clear();
        std::memset(mImage->data(), 0, mImage->getTotalSizeInBytes());

        const osg::ref_ptr<osg::Image> rgba = asRgba(std::move(image));
        resampleRegion(*rgba, source, *mImage, destination);

        mTexture->paintAll();
    }

    void TracedOverlay::replace(osg::ref_ptr<osg::Image> image)
    {
        mPending.clear();

        // Into the overlay's own image, so what the texture and its mirror draw from never changes
        // identity.
        const osg::ref_ptr<osg::Image> rgba = asRgba(std::move(image));
        assert(rgba->s() == mImage->s() && rgba->t() == mImage->t());
        std::memcpy(mImage->data(), rgba->data(), mImage->getTotalSizeInBytes());

        mTexture->paintAll();
    }

    void TracedOverlay::clear()
    {
        mPending.clear();
        std::memset(mImage->data(), 0, mImage->getTotalSizeInBytes());
        mTexture->paintAll();
    }
}
