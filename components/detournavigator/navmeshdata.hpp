#pragma once

#include <memory>

#include <DetourAlloc.h>

namespace DetourNavigator
{
    struct NavMeshDataValueDeleter
    {
        void operator()(unsigned char* value) const { dtFree(value); }
    };

    using NavMeshDataValue = std::unique_ptr<unsigned char, NavMeshDataValueDeleter>;

    struct NavMeshData
    {
        NavMeshDataValue mValue;
        int mSize = 0;

        NavMeshData() = default;

        NavMeshData(unsigned char* value, int size)
            : mValue(value)
            , mSize(size)
        {
        }
    };
}
