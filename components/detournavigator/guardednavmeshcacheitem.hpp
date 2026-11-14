#pragma once

namespace Misc
{
    template <class T>
    class ScopeGuarded;
}

namespace DetourNavigator
{
    class NavMeshCacheItem;

    using GuardedNavMeshCacheItem = Misc::ScopeGuarded<NavMeshCacheItem>;
}
