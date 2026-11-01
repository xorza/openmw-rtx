#pragma once

namespace Rtx
{
    /// Whether a new key rehashes `map`, moving every entry: what each map a walk fills is reserved
    /// against, and counted where it happens anyway (`SceneExtractor::countPastRoom`), since the
    /// content decides how much a cell arrives with, and a world is never an assert.
    template <class Map>
    bool fillsRoom(const Map& map)
    {
        return map.size() >= map.max_load();
    }
}
