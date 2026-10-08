#include "skybuilder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <osg/Image>
#include <osg/Vec2f>
#include <osg/ref_ptr>

#include <components/misc/resourcehelpers.hpp>
#include <components/misc/result.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "atmospheremesh.hpp"
#include "cloudmesh.hpp"
#include "skysheet.hpp"
#include "starmesh.hpp"

namespace Rtx
{
    std::uint32_t addCloudSheet(SceneDesc& scene, Resource::SceneManager& scenes, ThreadContent& thread,
        std::vector<TextureHold>& holds, std::string_view name, SkyContent& content)
    {
        const auto at = static_cast<std::uint32_t>(content.mSheets.size());
        CloudSheet& sheet = content.mSheets.emplace_back(CloudSheet{ .mName = std::string(name) });
        const VFS::Manager& vfs = *scenes.getVFS();

        // `Morrowind.ini` spells every deck `.tga` and every one ships as `.dds`, so a name joined
        // by hand leaves the two weathers an importer writes with no deck, in silence.
        // `correctTexturePath` is the same question `mwrender/sky.cpp` asks.
        const VFS::Path::Normalized path
            = Misc::ResourceHelpers::correctTexturePath(VFS::Path::toNormalized(name), vfs);
        if (!vfs.exists(path))
        {
            scene.refusals().refuse(Refused::SkyLayer, path.value(), "the archives hold no such file");
            return at;
        }

        // Opened here, where the deck's mean and cover are read off the image anyway: a sheet the
        // upload cannot take takes no slot, which the device reads as no deck.
        const Misc::Result<osg::ref_ptr<const osg::Image>, std::string> image
            = openImage(*scenes.getImageManager(), path);
        TextureHold deck = takeSkySheet(scene, path, image);
        if (deck.get() == sNoIndex)
            return at;

        sheet.mTexture = deck.get();
        holds.push_back(std::move(deck));

        // Averaging a 512-square sheet is a quarter of a million texels; the image is the one the
        // upload is about to take out of the same cache.
        const MeanTexel& painted = thread.meanOf(*image.value());
        sheet.mMean = painted.opaque() * Shaders::LUMINANCE_WEIGHTS;
        return at;
    }

    SkyContent addSkyContent(SceneDesc& scene, Resource::SceneManager& scenes, const SkySources& sources,
        ThreadContent& thread, std::vector<TextureHold>& holds)
    {
        SkyContent loaded;

        // Every weather's sheet now, under a megabyte for all ten, so a storm arriving costs no
        // upload. Empty where the weather names none, which the shipped fallbacks do for ash and
        // blight; named once where two weathers share one.
        for (const std::string& sheet : sources.mCloudSheets)
            if (!sheet.empty() && loaded.sheetNamed(sheet) == sNoSheet)
                addCloudSheet(scene, scenes, thread, holds, sheet, loaded);

        // The shape the deck hangs on is the mesh's, both of its numbers: how high the layer is
        // in tiles of its own sheet, and how far it falls away over the ground it covers.
        if (const Misc::Result<CloudShell, std::string> shell = readCloudShell(scenes, sources.mClouds); shell.isOk())
            loaded.mShell = shell.value();
        else
            scene.refusals().refuse(Refused::SkyLayer, sources.mClouds.value(), shell.error());

        // Where the fog colour fades to the sky colour is the atmosphere mesh's.
        if (const Misc::Result<Atmosphere, std::string> atmosphere = readAtmosphere(scenes, sources.mAtmosphere);
            atmosphere.isOk())
            loaded.mAtmosphere = atmosphere.value();
        else
            scene.refusals().refuse(Refused::SkyLayer, sources.mAtmosphere.value(), atmosphere.error());

        // The night sky is the mesh's, every number of it: which sheet the field wears, how much
        // sky a tile of it covers, where it fades out, and where the six patches sit.
        if (const Misc::Result<NightSky, std::string> night
            = readNightSky(scene, scenes, sources.mStars, sources.mStarsFallback, thread, holds);
            night.isOk())
            loaded.mNight = night.value();
        else
            scene.refusals().refuse(Refused::SkyLayer, sources.mStars.value(), night.error());

        return loaded;
    }
}
