#pragma once

#include <string>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/misc/result.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    class SceneDesc;

    /// The slot a sky's sheet stands in — a cloud deck's, the star field's, a moon's face — held for
    /// the caller. Refused as a sky layer, and an empty hold, where no image reads from the file or the
    /// upload cannot take it: no slot stands in, and a deck, a field and a moon each draw what they
    /// have without one. Asked before a slot is taken, because one the upload refused would stand
    /// in as an opaque grey.
    TextureHold takeSkySheet(SceneDesc& scene, VFS::Path::NormalizedView path,
        const Misc::Result<osg::ref_ptr<const osg::Image>, std::string>& image, TextureWrap wrap = TextureWrap::Repeat);
}
