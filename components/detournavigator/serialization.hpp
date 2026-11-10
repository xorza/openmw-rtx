#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_SERIALIZATION_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_SERIALIZATION_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DetourNavigator
{
    struct PreparedNavMeshData;

    constexpr char preparedNavMeshDataMagic[] = { 'p', 'n', 'a', 'v' };
    constexpr std::uint32_t preparedNavMeshDataVersion = 1;

    std::vector<std::byte> serialize(const PreparedNavMeshData& value);

    bool deserialize(const std::vector<std::byte>& data, PreparedNavMeshData& value);
}

#endif
