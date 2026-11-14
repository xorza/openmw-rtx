#pragma once

#include "guardednavmeshcacheitem.hpp"

#include <memory>

namespace DetourNavigator
{
    using SharedNavMeshCacheItem = std::shared_ptr<GuardedNavMeshCacheItem>;
}
