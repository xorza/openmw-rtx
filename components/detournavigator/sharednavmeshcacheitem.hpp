#pragma once

#include <memory>

#include "guardednavmeshcacheitem.hpp"

namespace DetourNavigator
{
    using SharedNavMeshCacheItem = std::shared_ptr<GuardedNavMeshCacheItem>;
}
