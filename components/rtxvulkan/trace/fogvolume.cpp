#include "fogvolume.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/storageformat.h>
#include <components/rtx/world/fogbuilder.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/fogvolume.h>

namespace Rtx
{
    FogTile::FogTile(const Device& device, const FogNoise& noise)
        : mField(device, Shaders::FOG_FIELD_SIZE, Shaders::FOG_FIELD_SIZE, VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "fog field", Shaders::FOG_FIELD_LEVELS,
            Shaders::FOG_FIELD_SIZE)
        , mSampler(makeContentSampler(device, "fog field"))
    {
        describe(device, noise);
    }

    void FogTile::describe(const Device& device, const FogNoise& noise)
    {
        Crash::contract(noise.mOffsets.size() == Shaders::FOG_FIELD_LEVELS, "a fog field with another chain");
        std::size_t texels = 0;
        for (std::uint32_t level = 0; level < Shaders::FOG_FIELD_LEVELS; ++level)
            texels += std::size_t{ mField.getWidthAt(level) } * mField.getHeightAt(level) * mField.getDepthAt(level);
        Crash::contract(noise.mBytes.size() == 2 * texels, "a fog field of another size");

        // Every level uploaded rather than halved from the one above, because each is stretched
        // back to one spread (`FogNoise::shared`). Seventy-three kilobytes, once.
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(Shaders::FOG_FIELD_LEVELS);
        for (std::uint32_t level = 0; level < Shaders::FOG_FIELD_LEVELS; ++level)
            regions.push_back(wholeLevel(noise.mOffsets[level], level,
                VkExtent3D{ mField.getWidthAt(level), mField.getHeightAt(level), mField.getDepthAt(level) }));

        Batch batch(device.getPool());
        uploadImage(batch, mField, std::as_bytes(std::span(noise.mBytes)), regions);
        batch.flush();
    }

    namespace
    {
        /// Half floats, because nothing here holds a quantity that grows and nothing sums these:
        /// the sun's is a product of transmittances, and the integrated pair is bounded by the
        /// transmittance beside it.
        constexpr VkFormat sFormat = toVulkanFormat(FOG_VOLUME_FORMAT);

        /// What the scatter pass's two answers are kept in, which the next frame's reads back into
        /// its own blend: `FOG_HISTORY_FORMAT` says why that is never a half.
        constexpr VkFormat sHistoryFormat = toVulkanFormat(FOG_HISTORY_FORMAT);
        static_assert(!Shaders::mayRoundTowardNought(FOG_HISTORY_FORMAT),
            "a history read back into its own blend is stored where a store may round toward nought");

        /// `TRANSFER_DST` because the constructor empties every one of these, which is what a
        /// history read before anything has written it needs and what an image made over a departed
        /// cell's memory has no other way of getting.
        constexpr VkImageUsageFlags sUsage
            = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        /// What the set holds, which `shaders/fogvolume.h` states for this side and the shaders
        /// that declare the same slots.
        constexpr std::uint32_t sBindings = Shaders::FOG_BINDING_COUNT;

        constexpr std::uint32_t sSampled = Shaders::FOG_SAMPLED_COUNT;

        constexpr bool sampledAt(std::uint32_t binding)
        {
            return binding < sSampled;
        }

        std::uint32_t columnsFor(std::uint32_t pixels)
        {
            return groupsFor(pixels, Shaders::FOG_VOLUME_SCALE);
        }

        /// The volume's images, in the order `FogVolume` declares them.
        enum class FogImage : std::uint8_t
        {
            Scatter0,
            Scatter1,
            Sunward0,
            Sunward1,
            Lamps,
            Air,
            AirSunward,
            Slice,
            SliceSunward,
            Seeing,
            ColumnDepth,
            ColumnMoons,
        };

        /// What each image is but its grid, which is the volume's: one table for what the
        /// constructor makes and what `FogVolume::bytesAt` measures.
        struct FogImageKind
        {
            VkFormat mFormat;
            VkImageUsageFlags mUsage;
            std::uint32_t mSlices;
            std::string_view mName;
        };

        constexpr VkImageUsageFlags sColumnUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        constexpr std::array<FogImageKind, static_cast<std::size_t>(FogImage::ColumnMoons) + 1> sFogImages{ {
            { sHistoryFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog scatter 0" },
            { sHistoryFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog scatter 1" },
            { sHistoryFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog sunward 0" },
            { sHistoryFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog sunward 1" },
            { sFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog lamps" },
            { sFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog air" },
            { toVulkanFormat(FOG_SUNWARD_FORMAT), sUsage, Shaders::FOG_VOLUME_SLICES, "fog air sunward" },
            { sFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog slice" },
            { toVulkanFormat(FOG_SUNWARD_FORMAT), sUsage, Shaders::FOG_VOLUME_SLICES, "fog slice sunward" },
            { sFormat, sUsage, Shaders::FOG_VOLUME_SLICES, "fog seeing" },
            { toVulkanFormat(FOG_DEPTH_FORMAT), sColumnUsage, 1, "fog column depth" },
            { toVulkanFormat(FOG_MOONS_FORMAT), sColumnUsage, Shaders::MOON_COUNT, "fog column moons" },
        } };

        static_assert(std::ranges::none_of(sFogImages, [](const FogImageKind& kind) { return kind.mName.empty(); }),
            "a fog image the table did not fill");

        const FogImageKind& kindOf(const FogImage image)
        {
            return sFogImages[static_cast<std::size_t>(image)];
        }

        ImageDescription descriptionOf(const FogImage image, const std::uint32_t columns, const std::uint32_t rows)
        {
            const FogImageKind& kind = kindOf(image);
            return ImageDescription{ .mWidth = columns,
                .mHeight = rows,
                .mFormat = kind.mFormat,
                .mUsage = kind.mUsage,
                .mDepth = kind.mSlices };
        }

        Image makeFogImage(const Device& device, const MemoryUse use, const FogImage image, const std::uint32_t columns,
            const std::uint32_t rows)
        {
            return Image(use, device, descriptionOf(image, columns, rows), kindOf(image).mName);
        }

        /// Sampled where a pass reads and storage where it writes. One image is named twice wherever
        /// both happen, because Vulkan has no one descriptor that is both. One table serves the
        /// layout and the pool that holds two sets of it.
        constexpr std::array<VkDescriptorSetLayoutBinding, sBindings> sLayoutBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, sBindings> bindings{};
            for (std::uint32_t binding = 0; binding < bindings.size(); ++binding)
                bindings[binding] = VkDescriptorSetLayoutBinding{ binding,
                    sampledAt(binding) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    1,
                    // The volume writes these as a dispatch, and the trace samples them from its
                    // ray generation shader and from the closest-hit shaders that shade what a
                    // bounce found.
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                        | VK_SHADER_STAGE_MISS_BIT_KHR,
                    nullptr };

            return bindings;
        }();
    }

    SetLayout FogVolume::describeLayout(const Device& device)
    {
        return makeSetLayout(device, sLayoutBindings);
    }

    VkDeviceSize FogVolume::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        VkDeviceSize bytes = 0;
        for (std::size_t image = 0; image < sFogImages.size(); ++image)
            bytes += Image::bytesFor(
                device, descriptionOf(static_cast<FogImage>(image), columnsFor(width), columnsFor(height)));
        return bytes;
    }

    FogVolume::FogVolume(const Device& device, const SetLayout& layout, const std::uint32_t width,
        const std::uint32_t height, const MemoryUse use)
        : mColumns(columnsFor(width))
        , mRows(columnsFor(height))
        , mScatter{ makeFogImage(device, use, FogImage::Scatter0, mColumns, mRows),
            makeFogImage(device, use, FogImage::Scatter1, mColumns, mRows) }
        , mSunward{ makeFogImage(device, use, FogImage::Sunward0, mColumns, mRows),
            makeFogImage(device, use, FogImage::Sunward1, mColumns, mRows) }
        , mLamps(makeFogImage(device, use, FogImage::Lamps, mColumns, mRows))
        , mAir(makeFogImage(device, use, FogImage::Air, mColumns, mRows))
        , mAirSunward(makeFogImage(device, use, FogImage::AirSunward, mColumns, mRows))
        , mSlice(makeFogImage(device, use, FogImage::Slice, mColumns, mRows))
        , mSliceSunward(makeFogImage(device, use, FogImage::SliceSunward, mColumns, mRows))
        , mSeeing(makeFogImage(device, use, FogImage::Seeing, mColumns, mRows))
        , mColumnDepth(makeFogImage(device, use, FogImage::ColumnDepth, mColumns, mRows))
        , mColumnMoons(makeFogImage(device, use, FogImage::ColumnMoons, mColumns, mRows))
        , mSampler(makeTargetSampler(device, "fog volume"))
        , mSets(device, sLayoutBindings, layout.get(), sParities)
    {
        // Sampled from `GENERAL` rather than moved to a read-only layout, for the reason
        // `BloomPass` gives: these are written as storage images and read as sampled ones a
        // dispatch apart, and `GENERAL` is the one layout both accesses are legal from.
        for (std::size_t parity = 0; parity < sParities; ++parity)
        {
            const std::size_t written = parity;
            const std::size_t history = 1 - parity;

            std::array<const Image*, sBindings> named{};
            named[Shaders::BIND_FOG_WAS_SCATTER] = &mScatter[history];
            named[Shaders::BIND_FOG_WAS_SUNWARD] = &mSunward[history];
            named[Shaders::BIND_FOG_SCATTER] = &mScatter[written];
            named[Shaders::BIND_FOG_SUNWARD] = &mSunward[written];
            named[Shaders::BIND_FOG_LAMPS] = &mLamps;
            named[Shaders::BIND_FOG_AIR] = &mAir;
            named[Shaders::BIND_FOG_AIR_SUNWARD] = &mAirSunward;
            named[Shaders::BIND_FOG_SLICE] = &mSlice;
            named[Shaders::BIND_FOG_SLICE_SUNWARD] = &mSliceSunward;
            named[Shaders::BIND_FOG_SEEING] = &mSeeing;
            named[Shaders::BIND_FOG_SCATTER_TARGET] = &mScatter[written];
            named[Shaders::BIND_FOG_SUNWARD_TARGET] = &mSunward[written];
            named[Shaders::BIND_FOG_LAMPS_TARGET] = &mLamps;
            named[Shaders::BIND_FOG_AIR_TARGET] = &mAir;
            named[Shaders::BIND_FOG_AIR_SUNWARD_TARGET] = &mAirSunward;
            named[Shaders::BIND_FOG_SLICE_TARGET] = &mSlice;
            named[Shaders::BIND_FOG_SLICE_SUNWARD_TARGET] = &mSliceSunward;
            named[Shaders::BIND_FOG_SEEING_TARGET] = &mSeeing;
            named[Shaders::BIND_FOG_COLUMN_DEPTH] = &mColumnDepth;
            named[Shaders::BIND_FOG_COLUMN_MOONS] = &mColumnMoons;

            DescriptorWrites writes(layout, mSets.get(parity));
            for (std::uint32_t binding = 0; binding < sBindings; ++binding)
                if (sampledAt(binding))
                    writes.image(binding, named[binding]->describeSampled(mSampler.get()));
                else
                    writes.image(binding, named[binding]->describeStorage());

            updateSets(device, writes.get());
        }

        // Emptied and in `GENERAL` from the moment they exist: `begin` does not discard the point
        // pair, so the first frame reads a history nothing has written, and over a suballocator's
        // range that is a departed image's bits rather than the driver's zeroed pages.
        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            constexpr VkClearColorValue nothing{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };

            for (const Image* image : { &mScatter[0], &mScatter[1], &mSunward[0], &mSunward[1], &mLamps, &mAir,
                     &mAirSunward, &mSlice, &mSliceSunward, &mSeeing, &mColumnDepth, &mColumnMoons })
                image->clear(commands, Use::sUndefined, nothing, Use::sAnyGeneral);
        });
    }

    void FogVolume::begin(VkCommandBuffer commands) const
    {
        const std::size_t written = mNow;

        // Discarded, because every texel of it is written before any is read; the other half of
        // the pair is this frame's history and survives. The point pair, the lamps and the column
        // images are written by the two launches, the integrated ones and the averaged seeing by the
        // dispatch after them. The last frame's readers are behind the head barrier
        // `CommandPool::begin` recorded — and so is the launch that wrote the history, which is why
        // the history takes no barrier of its own: it rests in `GENERAL`, and the head barrier made
        // the write visible to every read after it.
        Barriers barriers(commands);
        for (const Image* image : { &mScatter[written], &mSunward[written], &mLamps, &mColumnDepth, &mColumnMoons })
            image->addTransition(barriers, Use::sUndefined, Use::sTraceWrite);
        for (const Image* image : { &mAir, &mAirSunward, &mSlice, &mSliceSunward, &mSeeing })
            image->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);

        barriers.flush();
    }

    void FogVolume::depthTaken(VkCommandBuffer commands) const
    {
        // The depth is loaded by the scatter launch, by the integrate dispatch and by the trace,
        // so the one hand-over names both stages; the moons by the scatter launch alone.
        Barriers barriers(commands);
        mColumnDepth.addTransition(barriers, Use::sTraceWrite, Use::sShaderStorageRead);
        mColumnMoons.addTransition(barriers, Use::sTraceWrite, Use::sTraceRead);
        barriers.flush();
    }

    void FogVolume::scattered(VkCommandBuffer commands) const
    {
        const std::size_t written = mNow;

        // `GENERAL` to `GENERAL`, so what this orders is the writes against the reads and nothing
        // else. Against the trace as well as the integrate pass, because a puff of smoke reads what
        // the lamps deliver at a point (`puffLight`).
        Barriers barriers(commands);
        for (const Image* image : { &mScatter[written], &mSunward[written], &mLamps })
            image->addTransition(barriers, Use::sTraceWrite, Use::sShaderSample);

        barriers.flush();
    }

    void FogVolume::handOver(VkCommandBuffer commands) const
    {
        Barriers barriers(commands);
        for (const Image* image : { &mAir, &mAirSunward, &mSlice, &mSliceSunward, &mSeeing })
            image->addTransition(barriers, Use::sComputeWrite, Use::sShaderSample);

        barriers.flush();
    }
}
