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

    /// **What the device made of what it was handed, since the last take** (`SceneHolder::takeAnswer`): what it could
    /// not stand, what each mesh's structure took at its build, which a static mesh rests on (`MeshTable::stand`), and
    /// the textures it had no room for. One answer, so one call carries everything the device says of an arrival.
    struct DeviceAnswer
    {
        std::vector<Refusal> mRefused;
        std::vector<StoodMesh> mMeshes;

        /// The texture slots the device had no room for while room is coming, which draw the stand-in until the
        /// scene's owner hands them over again (`SceneUploader`): the device keeps no texels to stand one later.
        std::vector<Index> mRoomless;

        void clear()
        {
            mRefused.clear();
            mMeshes.clear();
            mRoomless.clear();
        }
    };
}
