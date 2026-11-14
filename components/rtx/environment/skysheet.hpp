#pragma once

#include <osg/Image>

#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    class SceneDesc;

    /// The slot a sky's sheet stands in — a cloud deck's, the star field's, a moon's face — held for
    /// the caller. Refused as a sky layer, and an empty hold, where the upload cannot take `image`:
    /// no slot stands in, and a deck, a field and a moon each draw what they have without one.
    /// Asked before a slot is taken, because one the upload refused would stand in as an opaque grey.
    TextureHold takeSkySheet(SceneDesc& scene, VFS::Path::NormalizedView path, const osg::Image& image,
        TextureWrap wrap = TextureWrap::Repeat);
}
