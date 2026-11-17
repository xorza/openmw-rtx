#pragma once

#include <cstdint>
#include <memory>

#include <DetourAlloc.h>

namespace DetourNavigator
{
    struct NavMeshDataValueDeleter
    {
        void operator()(std::uint8_t* value) const { dtFree(value); }
    };

    using NavMeshDataValue = std::unique_ptr<std::uint8_t, NavMeshDataValueDeleter>;

    struct NavMeshData
    {
        NavMeshDataValue mValue;
        std::int32_t mSize = 0;

        NavMeshData() = default;

        NavMeshData(std::uint8_t* value, std::int32_t size)
            : mValue(value)
            , mSize(size)
        {
        }
    };
}
