#include "contentmodels.hpp"

#include <algorithm>

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/terrain/storage.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "../terrainstorage.hpp"
#include "skyreader.hpp"

namespace MWRender
{
    namespace
    {
        template <class Record>
        void addModels(const MWWorld::ESMStore& store, const VFS::Manager& vfs, std::vector<std::string>& into)
        {
            for (const Record& record : store.get<Record>())
            {
                if (record.mModel.empty())
                    continue;
                const VFS::Path::Normalized model
                    = Misc::ResourceHelpers::correctMeshPath(record.mModel.getNormalized());
                into.emplace_back(model.value());

                // The model `Objects` loads for an animated object or an actor where its `x` sibling
                // stands, which is another file than the one the record names.
                const VFS::Path::Normalized animated = Misc::ResourceHelpers::correctActorModelPath(model, &vfs);
                if (animated != model)
                    into.emplace_back(animated.value());
            }
        }
    }

    std::vector<std::string> contentModels(const MWWorld::ESMStore& store, Resource::ResourceSystem& resources)
    {
        const VFS::Manager& vfs = *resources.getVFS();
        std::vector<std::string> models;
        addModels<ESM::Activator>(store, vfs, models);
        addModels<ESM::Potion>(store, vfs, models);
        addModels<ESM::Apparatus>(store, vfs, models);
        addModels<ESM::Armor>(store, vfs, models);
        addModels<ESM::BodyPart>(store, vfs, models);
        addModels<ESM::Book>(store, vfs, models);
        addModels<ESM::Clothing>(store, vfs, models);
        addModels<ESM::Container>(store, vfs, models);
        addModels<ESM::Creature>(store, vfs, models);
        addModels<ESM::Door>(store, vfs, models);
        addModels<ESM::Ingredient>(store, vfs, models);
        addModels<ESM::Light>(store, vfs, models);
        addModels<ESM::Lockpick>(store, vfs, models);
        addModels<ESM::Miscellaneous>(store, vfs, models);
        addModels<ESM::NPC>(store, vfs, models);
        addModels<ESM::Probe>(store, vfs, models);
        addModels<ESM::Repair>(store, vfs, models);
        addModels<ESM::Static>(store, vfs, models);
        addModels<ESM::Weapon>(store, vfs, models);

        std::vector<VFS::Path::Normalized> sky;
        std::vector<VFS::Path::Normalized> skyTextures;
        SkyReader::listAssets(vfs, sky, skyTextures);
        for (const VFS::Path::Normalized& model : sky)
            models.emplace_back(model.value());

        std::sort(models.begin(), models.end());
        models.erase(std::unique(models.begin(), models.end()), models.end());
        return models;
    }

    std::vector<Rtx::PreparationLayer> contentLayers(
        const MWWorld::ESMStore& store, Resource::ResourceSystem& resources)
    {
        // A storage of its own for its rule alone: the game's is the rendering manager's, which stands
        // after the loading screen, and is made with these settings (`RenderingManager`).
        const std::string& normalMapPattern = Settings::shaders().mNormalMapPattern;
        const std::string& heightMapPattern = Settings::shaders().mNormalHeightMapPattern;
        const std::string& specularMapPattern = Settings::shaders().mTerrainSpecularMapPattern;
        TerrainStorage storage(&resources, normalMapPattern, heightMapPattern,
            Settings::shaders().mAutoUseTerrainNormalMaps, specularMapPattern,
            Settings::shaders().mAutoUseTerrainSpecularMaps);

        const VFS::Manager& vfs = *resources.getVFS();
        std::vector<Rtx::PreparationLayer> layers;
        const auto add = [&](VFS::Path::NormalizedView texture) {
            const Terrain::LayerInfo info
                = storage.getLayerInfo(Misc::ResourceHelpers::correctTexturePath(texture, vfs));
            layers.push_back(Rtx::PreparationLayer{ .mDiffuse = std::string(info.mDiffuseMap.value()),
                .mNormal = std::string(info.mNormalMap.value()),
                .mParallax = info.mParallax,
                .mDiffuseSpec = info.mSpecular });
        };
        add(VFS::Path::NormalizedView("_land_default.dds"));
        for (const auto& [id, path] : store.get<ESM::LandTexture>())
            add(path.getNormalized());

        std::sort(layers.begin(), layers.end(),
            [](const Rtx::PreparationLayer& a, const Rtx::PreparationLayer& b) { return a.mDiffuse < b.mDiffuse; });
        layers.erase(std::unique(layers.begin(), layers.end(),
                         [](const Rtx::PreparationLayer& a, const Rtx::PreparationLayer& b) {
                             return a.mDiffuse == b.mDiffuse;
                         }),
            layers.end());
        return layers;
    }
}
