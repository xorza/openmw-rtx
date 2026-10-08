#include "moonfaces.hpp"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>

#include "skysheet.hpp"

namespace Rtx
{
    namespace
    {
        /// How wide `size` draws `moon`. `Fallback::Map` answers a key the configuration leaves out,
        /// and one that does not parse, with nought, and the game draws a quad of no extent for
        /// that; an error for a size below nought, or one that is not finite.
        Misc::Result<float, std::string> radiusOf(Moon moon, float size)
        {
            if (!(size >= 0.0f) || !std::isfinite(size))
                return Misc::Err{ "Moons_" + std::string(sMoonNames.name(moon)) + "_Size is " + std::to_string(size)
                    + ", which is no size" };

            return moonAngularRadius(size);
        }
    }

    MoonFaces addMoonFaces(SceneDesc& scene, Resource::ImageManager& images, const MoonSizes& sizes,
        ThreadContent& thread, std::vector<TextureHold>& holds)
    {
        // A moon of a size that is no size is refused and not drawn.
        const auto drawnWidth = [&](Moon moon, float size) {
            const Misc::Result<float, std::string> radius = radiusOf(moon, size);
            if (radius.isOk())
                return radius.value();

            scene.refusals().refuse(Refused::Moon, sMoonNames.name(moon), radius.error());
            return 0.0f;
        };

        // Clamped: a portrait is one image edge to edge, and a repeating tap at its limb would
        // blend the far edge's paint into the disc's antialiasing. A face that does not open, or
        // that the upload cannot take, takes no slot, and the moon is drawn its mean colour. The mean
        // read here and not on a frame, as a cloud deck's is: a face that does not open keeps the
        // shipped portrait's.
        MoonFaces faces;
        for (std::size_t at = 0; at < sMoonCount; ++at)
        {
            const Moon moon = static_cast<Moon>(at);
            MoonFace& face = faces.of(moon);
            face.mRadius = drawnWidth(moon, sizes[at]);

            const VFS::Path::NormalizedView path = moonFaceOf(moon);
            const Misc::Result<osg::ref_ptr<const osg::Image>, std::string> image = openImage(images, path);
            TextureHold held = takeSkySheet(scene, path, image, TextureWrap::Clamp);
            face.mSlot = held.get();
            if (face.mSlot == sNoIndex)
                continue;

            holds.push_back(std::move(held));
            face.mMean = thread.meanOf(*image.value()).opaque();
        }
        return faces;
    }
}
