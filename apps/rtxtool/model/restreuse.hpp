#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <components/rtx/common/index.hpp>

namespace Rtx
{
    class SceneDesc;
}

namespace RtxTool
{
    /// **How content comes back from rest, as a cache of every size would have kept it**: each texture or mesh that
    /// wakes is counted by its own bytes and the bytes of either kind that came to rest after it and still rest, the
    /// least cache of what rests that would still hold it — its stack distance, which gives the hits of a
    /// least-recently-rested cache of every size in one run (Mattson et al. 1970, "Evaluation techniques for storage
    /// hierarchies"). What sets the bound on the bytes that rest (`TextureTable`).
    class RestReuse
    {
    public:
        /// The bytes woken are counted in steps of this, `sSteps` of them, 16 GiB, past which no cache is asked of.
        static constexpr std::uint64_t sStep = std::uint64_t{ 32 } << 20;
        static constexpr std::size_t sSteps = 512;

        RestReuse();

        /// Reads which textures and meshes of `scene` woke from rest since the read before, and what rested after each,
        /// in the scene's one order. Once a frame: a row woken and laid to rest again between two reads is not seen.
        void read(const Rtx::SceneDesc& scene);

        /// The bytes woken, and of them what a cache of `bytes` would have kept: a whole step at most.
        std::uint64_t getWoken() const { return mWoken; }
        std::uint64_t keptUnder(std::uint64_t bytes) const;

        /// The bytes woken, and the share a cache of each size from 64 MiB, doubling, would have kept.
        std::string describe() const;

    private:
        /// One row at rest as the read before saw it: a mesh's or a texture's slot, when it was filed, what it stood
        /// on the device, and the bytes that came to rest after it and still rest.
        struct Rested
        {
            bool mMesh = false;
            Rtx::Index mSlot = 0;
            std::uint64_t mFiledAt = 0;
            std::uint64_t mBytes = 0;
            std::uint64_t mAfter = 0;
        };

        /// What rested at the read before, oldest first, refilled each read into room for every slot of both tables.
        std::vector<Rested> mRested;

        std::array<std::uint64_t, sSteps> mSteps{};
        std::uint64_t mWoken = 0;
    };
}
