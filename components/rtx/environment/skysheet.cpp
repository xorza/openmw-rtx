#include "skysheet.hpp"

#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace Rtx
{
    TextureHold takeSkySheet(SceneDesc& scene, const VFS::Path::NormalizedView path,
        const Misc::Result<osg::ref_ptr<const osg::Image>, std::string>& image, const TextureWrap wrap)
    {
        if (!image.isOk())
        {
            scene.refusals().refuse(Refused::SkyLayer, path.value(), image.error());
            return TextureHold{};
        }

        if (const Misc::Result<void, std::string> uploadable = checkUploadable(*image.value()); !uploadable.isOk())
        {
            scene.refusals().refuse(Refused::SkyLayer, path.value(), uploadable.error());
            return TextureHold{};
        }

        return scene.takeTexture(path, *image.value(), wrap);
    }
}
