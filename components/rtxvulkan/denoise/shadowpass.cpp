#include "shadowpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_MASK_BINDINGS> sMaskBindings
            = computeBindings<Shaders::SHADOW_MASK_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_TILES_BINDINGS> sTilesBindings
            = computeBindings<Shaders::SHADOW_TILES_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_FILTER_BINDINGS> sFilterBindings
            = computeBindings<Shaders::SHADOW_FILTER_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        /// Each filter's name, by level and by its fields less one: a name a pipeline alone has, so a
        /// capture, a compile time or a fault names the one it means.
        constexpr std::array<std::array<std::string_view, 3>, 3> sFilterNames{ {
            { "shadow-filter-0-sky", "shadow-filter-0-lamps", "shadow-filter-0-both" },
            { "shadow-filter-1-sky", "shadow-filter-1-lamps", "shadow-filter-1-both" },
            { "shadow-filter-2-sky", "shadow-filter-2-lamps", "shadow-filter-2-both" },
        } };

        /// The level's module over the fields `fields`' bits name, `SHADOW_SPEC_FIELDS`.
        ComputePipeline<Shaders::ShadowFilterConstants> makeFilter(
            const Device& device, const std::uint32_t level, const std::uint32_t fields)
        {
            std::array<std::uint32_t, Shaders::SHADOW_SPEC_COUNT> specialization{};
            specialization[Shaders::SHADOW_SPEC_LEVEL] = level;
            specialization[Shaders::SHADOW_SPEC_FIELDS] = fields;
            return ComputePipeline<Shaders::ShadowFilterConstants>(
                device, sFilterBindings, {}, "shadowfilter.comp.spv", sFilterNames[level][fields - 1], specialization);
        }

        /// The temporal pass, over a field of fractions where `fraction`.
        ComputePipeline<Shaders::ShadowTilesConstants> makeTiles(const Device& device, const bool fraction)
        {
            std::array<std::uint32_t, Shaders::SHADOW_TILES_SPEC_COUNT> specialization{};
            specialization[Shaders::SHADOW_TILES_SPEC_FRACTION] = fraction ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::ShadowTilesConstants>(device, sTilesBindings, {}, "shadowtiles.comp.spv",
                fraction ? "shadow-tiles-fraction" : "shadow-tiles", specialization);
        }

        std::array<ComputePipeline<Shaders::ShadowFilterConstants>, (1u << Shaders::SHADOW_FIELD_COUNT) - 1u>
        makeFilters(const Device& device, const std::uint32_t level)
        {
            return { makeFilter(device, level, 1u), makeFilter(device, level, 2u), makeFilter(device, level, 3u) };
        }
    }

    ShadowPass::ShadowPass(const Device& device)
        : mMask(device, sMaskBindings, {}, "shadowmask.comp.spv", "shadow-mask")
        , mTiles(makeTiles(device, false))
        , mFractionTiles(makeTiles(device, true))
        , mFilters{ makeFilters(device, 0), makeFilters(device, 1), makeFilters(device, 2) }
    {
    }

    void ShadowPass::recordMask(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        assert(images.mVisibility.getWidth() >= camera.mWidth);
        assert(images.mVisibility.getHeight() >= camera.mHeight);
        assert(buffer.getWidth() >= camera.mWidth);
        assert(buffer.getHeight() >= camera.mHeight);

        const Image& shadowed = buffer.get(images.mField == ShadowField::Sky ? Channel::Shadowed : Channel::Lamped);

        DescriptorWrites writes(mMask);
        writes.image(Shaders::SHADOW_MASK_BIND_SHADOWED, shadowed.describeStorage());
        writes.image(Shaders::SHADOW_MASK_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::SHADOW_MASK_BIND_MASK, images.mMask.describeStorage());
        writes.image(Shaders::SHADOW_MASK_BIND_PENUMBRA_TILES, images.mPenumbra.describeStorage());
        writes.image(Shaders::SHADOW_MASK_BIND_SHARE, buffer.get(Channel::LampedShare).describeStorage());

        dispatch(commands, mMask, writes,
            Shaders::ShadowMaskConstants{ .mWidth = camera.mWidth,
                .mHeight = camera.mHeight,
                .mFrame = frame.mSampled.mFrame,
                .mFraction = images.mField == ShadowField::Sky ? 0u : 1u },
            Groups::covering(
                camera.mWidth, camera.mHeight, Shaders::SHADOW_MASK_WIDTH, 2 * Shaders::SHADOW_MASK_HEIGHT));
    }

    void ShadowPass::recordTiles(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;

        // The sky's value is its rays' bit, and the lamps' the share of their light (`CHANNEL_LAMPED_SHARE`).
        const ComputePipeline<Shaders::ShadowTilesConstants>& tiles
            = images.mField == ShadowField::Sky ? mTiles : mFractionTiles;
        DescriptorWrites writes(tiles);
        writes.image(Shaders::SHADOW_TILES_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_HELD_SURFACE, buffer.getHeld(Channel::Surface).describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_HISTORY, images.mHistory.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS_BEFORE, images.mMomentsBefore.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS, images.mMoments.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_REPROJECTED, images.mScratch.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_TILES, images.mTiles.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_MASK, images.mMask.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_PENUMBRA_TILES, images.mPenumbra.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_MATCH, images.mMatch.describeStorage());
        writes.image(Shaders::SHADOW_TILES_BIND_SHARE, buffer.get(Channel::LampedShare).describeStorage());

        const Shaders::ShadowTilesConstants constants{
            .mHistory = frame.history(images.mFresh),
            .mField = static_cast<std::uint32_t>(images.mField),
        };

        dispatch(commands, tiles, writes, constants,
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::SHADOW_WORKGROUP));
    }

    void ShadowPass::recordLevel(VkCommandBuffer commands, const std::uint32_t level,
        const std::array<const DenoiseHistory::ShadowImages*, sShadowFields>& fields, const std::uint32_t filtering,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        assert(level < Shaders::SHADOW_FILTER_LEVELS);
        assert(
            filtering != 0u && filtering < (1u << sShadowFields) && "a level over no field, or one there is none of");
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        const ComputePipeline<Shaders::ShadowFilterConstants>& filter = mFilters[level][filtering - 1u];

        // Every field's images bound, the ones the module does not filter as well: a binding the
        // module names is one the push must write.
        DescriptorWrites writes(filter);
        writes.image(Shaders::SHADOW_FILTER_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        // The SDK's order: the temporal blend into the first level, whose answer is the history the
        // next frame reads; the second back into the scratch, which a cleared tile leaves holding the
        // temporal pass's exact value; and the third into what the composite reads. In the table's
        // order, a field after another at each binding.
        const auto source = [&](const DenoiseHistory::ShadowImages& images) -> const Image& {
            return level == 1 ? images.mHistory : images.mScratch;
        };
        const auto target = [&](const DenoiseHistory::ShadowImages& images) -> const Image& {
            return level == 0 ? images.mHistory : level == 1 ? images.mScratch : images.mVisibility;
        };
        for (std::uint32_t field = 0; field < sShadowFields; ++field)
        {
            assert(static_cast<std::uint32_t>(fields[field]->mField) == field && "a field's images at another's index");
            writes.image(Shaders::SHADOW_FILTER_BIND_TILES + field, fields[field]->mTiles.describeStorage());
        }
        for (std::uint32_t field = 0; field < sShadowFields; ++field)
            writes.image(Shaders::SHADOW_FILTER_BIND_SOURCE + field, source(*fields[field]).describeStorage());
        for (std::uint32_t field = 0; field < sShadowFields; ++field)
            writes.image(Shaders::SHADOW_FILTER_BIND_FILTERED + field, target(*fields[field]).describeStorage());

        dispatch(commands, filter, writes,
            Shaders::ShadowFilterConstants{ .mEyes = frame.mSampled.mEyes, .mFrame = frame.mSampled.mFrame },
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::SHADOW_WORKGROUP));
    }
}
