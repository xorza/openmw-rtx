#pragma once

#include <cassert>
#include <cstdint>
#include <iterator>
#include <variant>
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
        MeshIndex mMesh = sNoRow;
        std::uint64_t mStructureBytes = 0;
    };

    /// A texture the device stood short of its file for want of room: the bytes it stood — nought for the stand-in, a
    /// smaller side otherwise — which the scene's owner hands over again once the room holds the file
    /// (`SceneUploader`): the device keeps no texels to stand it whole later.
    struct ShortTexture
    {
        TextureIndex mSlot = sNoRow;
        std::uint64_t mStoodBytes = 0;

        /// What it stood before it moved down to make room (`TextureArray::makeRoom`), which the hand-over that moved
        /// it did not describe; nought for a texture the hand-over stood short as it arrived.
        std::uint64_t mWholeBytes = 0;

        bool operator==(const ShortTexture& other) const = default;
    };

    /// **A named texture the device moved down to make room while it stood finer than its readers want**
    /// (`TextureArray::makeRoom`): what it stands now, and the level of its file it stands from. Not short, since its
    /// readers want no finer, so its owner tells the slot what stands and hands nothing over again.
    struct MovedTexture
    {
        TextureIndex mSlot = sNoRow;
        std::uint64_t mStoodBytes = 0;
        std::uint8_t mFrom = 0;
    };

    /// **What waits on room, and so whose rest serves it** (`RoomWant`): a texture, served by the textures' rest in the
    /// pools textures stand in, or a mesh, by the meshes' rest in the storage their structures stand in once copied
    /// tight. The slot names the want from one take to the next.
    using RoomWaiter = std::variant<TextureIndex, MeshIndex>;

    /// **Content the device had no room for and waits on**, met since the last take (`DeviceAnswer::mWants`): what
    /// waits
    /// (`RoomWaiter`), and the range a resting row of its kind must give back (`RestPlace::mFit`) for its eviction to
    /// surely leave it room: its size, and the alignment its start needs. A range freed by a resource of its own kind
    /// holds it where it is as large and starts at an alignment at least as coarse: its neighbours held the resource
    /// before.
    struct RoomWant
    {
        RoomWaiter mWaiter;
        std::uint64_t mHole = 0;
        std::uint64_t mAlignment = 1;

        bool operator==(const RoomWant& other) const = default;
    };

    /// What a slot the device stands gives back where a want can take it once it rests (`RestPlace::mFit`): a texture
    /// its image's range of the pools textures stand in, a static mesh its tight copy's room in the structure storage.
    ///
    /// @tparam I the slot's table's row (`RowIndex`).
    template <class I>
    struct FitBytes
    {
        I mSlot = sNoRow;
        std::uint64_t mBytes = 0;

        /// The coarsest power of two the range's start is aligned to: what a want's alignment is matched against.
        std::uint64_t mAlignment = 1;

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
        std::vector<MovedTexture> mMoved;
        std::vector<RoomWant> mWants;
        std::vector<FitBytes<TextureIndex>> mTextureFits;
        std::vector<FitBytes<MeshIndex>> mMeshFits;

        void clear()
        {
            mRefused.clear();
            mMeshes.clear();
            mShort.clear();
            mMoved.clear();
            mWants.clear();
            mTextureFits.clear();
            mMeshFits.clear();
        }

        /// Hands everything this holds on to `into`, after what that holds already, and empties this. **The one place
        /// beside `clear` that names every list**, so a list added to the answer is carried by every holder that
        /// forwards one: a forwarder that named them itself left the moved textures behind.
        void handTo(DeviceAnswer& into)
        {
            assert(&into != this && "an answer handed to itself");
            const auto hand = [](auto& from, auto& to) {
                to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
                from.clear();
            };
            hand(mRefused, into.mRefused);
            hand(mMeshes, into.mMeshes);
            hand(mShort, into.mShort);
            hand(mMoved, into.mMoved);
            hand(mWants, into.mWants);
            hand(mTextureFits, into.mTextureFits);
            hand(mMeshFits, into.mMeshFits);
        }
    };

    static_assert(sizeof(DeviceAnswer) == 7 * sizeof(std::vector<Refusal>),
        "a list added to the answer is cleared and handed on with the rest");
}
