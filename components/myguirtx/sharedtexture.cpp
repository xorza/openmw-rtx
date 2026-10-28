#include "sharedtexture.hpp"

#include <osg/Image>
#include <osg/Texture2D>

namespace MyGUIRtx
{
    SharedTexture::SharedTexture(Rtx::GuiRenderer& renderer, osg::Texture2D& source)
        : MirrorTexture(renderer)
        , mSource(&source)
    {
        refresh();
    }

    SharedTexture::~SharedTexture() = default;

    void SharedTexture::refresh()
    {
        const osg::Image* const image = mSource->getImage();
        if (image == nullptr || image->s() <= 0 || image->t() <= 0)
            return;

        if (image == mSeen && image->getModifiedCount() == mSeenCount)
            return;

        mSeen = image;
        mSeenCount = image->getModifiedCount();

        // A picture of another shape is another slot: the table sizes a slot once, and a size it
        // refused is asked again only with another picture of another shape.
        if (!isSized(image->s(), image->t()))
            take(image->s(), image->t());

        sendImage(*image);
    }
}
