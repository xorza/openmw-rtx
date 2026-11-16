#pragma once

#include <cstdint>
#include <vector>

#include <components/rtx/common/index.hpp>

#include "refusal.hpp"

namespace Rtx
{
    /// What the device does with content it has no room for: waits while room is coming — textures resting that the
    /// scene's owner gives back, or memory on its way back — or refuses it where none is.
    enum class WhenRoomless : std::uint8_t
    {
        Waits,
        Refuses,
    };

    /// What one mesh's bottom-level structure took at its build (`DeviceAnswer`), the same in every process: its copy
    /// made tight lands whenever the device answers.
    struct StoodMesh
    {
        Index mMesh = sNoIndex;
        std::uint64_t mStructureBytes = 0;
    };

    /// A texture the device stood short of its file for want of room: the bytes it stood — nought for the stand-in, a
    /// smaller side otherwise — which the scene's owner hands over again once the room holds the file
    /// (`SceneUploader`): the device keeps no texels to stand it whole later.
    struct ShortTexture
    {
        Index mSlot = sNoIndex;
        std::uint64_t mStoodBytes = 0;

        bool operator==(const ShortTexture& other) const = default;
    };

    /// **What the device made of what it was handed, since the last take** (`SceneHolder::takeAnswer`): what it could
    /// not stand, what each mesh's structure took at its build, which a static mesh rests on (`MeshTable::stand`), and
    /// each texture it stood short for want of room. One answer, so one call carries everything the device says of an
    /// arrival.
    struct DeviceAnswer
    {
        std::vector<Refusal> mRefused;
        std::vector<StoodMesh> mMeshes;
        std::vector<ShortTexture> mShort;

        void clear()
        {
            mRefused.clear();
            mMeshes.clear();
            mShort.clear();
        }
    };
}
