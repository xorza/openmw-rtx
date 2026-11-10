#include "navigator.hpp"
#include "navigatorimpl.hpp"
#include "navigatorstub.hpp"
#include "navmeshtilestore.hpp"
#include "recastglobalallocator.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>

namespace DetourNavigator
{
    std::unique_ptr<Navigator> makeNavigator(const Settings& settings, const std::filesystem::path& cachePath)
    {
        DetourNavigator::RecastGlobalAllocator::init();

        std::unique_ptr<NavMeshTileStore> store;
        if (settings.mEnableNavMeshDiskCache)
        {
            const std::filesystem::path folder = cachePath / sNavMeshTileStoreFolder;
            Log(Debug::Info) << "Using " << Files::pathToUnicodeString(folder) << " to store navigation mesh tiles";
            store = std::make_unique<NavMeshTileStore>(folder);
        }

        return std::make_unique<NavigatorImpl>(settings, std::move(store));
    }

    std::unique_ptr<Navigator> makeNavigatorStub()
    {
        return std::make_unique<NavigatorStub>();
    }
}
