#include "skysheet.hpp"

#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace Rtx
{
    TextureHold takeSkySheet(
        SceneDesc& scene, const VFS::Path::NormalizedView path, const osg::Image& image, const TextureWrap wrap)
    {
        if (const Result<void, std::string> uploadable = checkUploadable(image); !uploadable.isOk())
        {
            scene.refusals().refuse(Refused::SkyLayer, path.value(), uploadable.error());
            return TextureHold{};
        }

        return scene.takeTexture(path, image, wrap);
    }
}
