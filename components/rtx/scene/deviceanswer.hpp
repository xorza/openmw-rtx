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

    /// Whose rest gives back the memory content the device waits on can take (`RoomWant`): textures', in the pools
    /// textures stand in, or meshes', in the storage their structures stand in once copied tight.
    enum class RestKind : std::uint8_t
    {
        Texture,
        Mesh,
    };

    /// **Content the device had no room for and waits on**, met since the last take (`DeviceAnswer::mWants`): whose
    /// rest serves it, the slot that waits, which names the want from one take to the next, and the bytes a resting row
    /// of that kind must give back where the want can take them (`RestPlace::mFit`) for its eviction to surely leave
    /// it room: its size, and what its alignment may leave unused at a hole's start.
    struct RoomWant
    {
        RestKind mKind = RestKind::Texture;
        Index mSlot = sNoIndex;
        std::uint64_t mHole = 0;

        bool operator==(const RoomWant& other) const = default;
    };

    /// What a slot the device stands gives back where a want can take it once it rests (`RestPlace::mFit`): a texture
    /// its image's range of the pools textures stand in, a static mesh its tight copy's room in the structure storage.
    struct FitBytes
    {
        Index mSlot = sNoIndex;
        std::uint64_t mBytes = 0;

        bool operator==(const FitBytes& other) const = default;
    };

    /// **What the device made of what it was handed, since the last take** (`SceneHolder::takeAnswer`): what it could
    /// not stand, what each mesh's structure took at its build, which a static mesh rests on (`MeshTable::stand`), each
    /// texture it stood short for want of room, what waits on room, and what each texture made and each tight copy
    /// gives back once it rests. One answer, so one call carries everything the device says of an
    /// arrival.
    struct DeviceAnswer
    {
        std::vector<Refusal> mRefused;
        std::vector<StoodMesh> mMeshes;
        std::vector<ShortTexture> mShort;
        std::vector<RoomWant> mWants;
        std::vector<FitBytes> mTextureFits;
        std::vector<FitBytes> mMeshFits;

        void clear()
        {
            mRefused.clear();
            mMeshes.clear();
            mShort.clear();
            mWants.clear();
            mTextureFits.clear();
            mMeshFits.clear();
        }
    };
}
