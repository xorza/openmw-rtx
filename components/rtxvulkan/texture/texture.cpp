#include "texture.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volk.h>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/stagingring.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/shaders/shared/ground.h>
#include <components/rtxvulkan/shaders/shared/normalspread.h>
#include <components/rtxvulkan/shaders/shared/sets.h>

#include "groundcompositepass.hpp"
#include "texturecost.hpp"
#include "texturepasses.hpp"

namespace Rtx
{
    namespace
    {
        /// What a written texture is stored as, which is what the shaders that write it declare.
        constexpr VkFormat sWrittenFormat = toVulkanFormat(TEXTURE_WRITTEN_FORMAT);

        /// What a texture made on the device is created as: the format its description states. The description's,
        /// because whether the trace reads it through the curve is a fact about the content (a bake is lighting). A
        /// dispatch stores through `sWrittenFormat`, the same bytes with the curve off, so a description may name that
        /// or its twin under the curve and nothing else.
        VkFormat writtenAs(const TextureFormat format)
        {
            const VkFormat image = toVulkanFormat(format);
            Crash::contract(
                withoutCurve(image) == sWrittenFormat, "a texture described in a format its dispatch does not store");
            return image;
        }

        /// Every stage that resolves a hit, and every dispatch. The trace's kernels read these
        /// arrays; the fog volume, the tone curve and the interface are dispatches and read them too.
        constexpr VkShaderStageFlags sStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        /// The layout every array declares, sized to the maximum and not to the scene, because a
        /// pipeline outlives a cell and two set layouts are compatible only where they are
        /// identically defined. The maximum is paid once: a sampled image is 64 B in RADV, so a
        /// set's image array is 8 MiB of pool. The samplers first, for the offsets `TEXTURE_BIND_IMAGES` says.
        constexpr std::array<VkDescriptorSetLayoutBinding, 2> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::TEXTURE_BIND_SAMPLERS, VK_DESCRIPTOR_TYPE_SAMPLER,
                Shaders::TEXTURE_SAMPLERS, sStages, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::TEXTURE_BIND_IMAGES, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                Shaders::TEXTURE_SLOTS * Shaders::TEXTURE_IMAGES_A_SLOT, sStages, nullptr },
        };

        static_assert(Shaders::TEXTURE_BIND_SAMPLERS < Shaders::TEXTURE_BIND_IMAGES);
        static_assert(Shaders::companionImageOf(Shaders::TEXTURE_NEUTRAL)
            < std::uint64_t{ Shaders::TEXTURE_SLOTS } * Shaders::TEXTURE_IMAGES_A_SLOT);

        static_assert(Shaders::TEXTURE_SAMPLERS_ALONG == sTextureWrapCount);
        static_assert(Shaders::TEXTURE_SAMPLERS == 2 * sTextureWrapCount);
        static_assert(Shaders::TEXTURE_TEXELS >> Shaders::TEXTURE_WRAP_SHIFT == 0);
        static_assert(sTextureWrapCount << Shaders::TEXTURE_WRAP_SHIFT == Shaders::TEXTURE_STANDS_IN);
        static_assert(
            std::uint64_t{ Shaders::TEXTURE_SIDE_MOST } * Shaders::TEXTURE_SIDE_MOST <= Shaders::TEXTURE_TEXELS);

        /// `sampler` as a sampler descriptor takes it.
        constexpr VkDescriptorImageInfo describeSampler(const VkSampler sampler)
        {
            return VkDescriptorImageInfo{ sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
        }

        /// One footprint sampler per `TextureWrap`, indexed by it, filtering by `anisotropy`.
        std::array<Sampler, sTextureWrapCount> makeFootprintSamplers(
            const Device& device, const std::uint32_t anisotropy)
        {
            return { makeFootprintSampler(device, "textures along repeating", TextureWrap::Repeat, anisotropy),
                makeFootprintSampler(device, "textures along clamped along s", TextureWrap::ClampS, anisotropy),
                makeFootprintSampler(device, "textures along clamped along t", TextureWrap::ClampT, anisotropy),
                makeFootprintSampler(device, "textures along clamped", TextureWrap::Clamp, anisotropy) };
        }
    }

    ImageDescription Texture::describe(const ImageShape& shape)
    {
        ImageDescription image{ .mWidth = shape.mWidth,
            .mHeight = shape.mHeight,
            .mFormat = toVulkanFormat(shape.mFormat),
            .mUsage = sUploadedTextureUsage,
            .mMipLevels = shape.mLevels };
        // A bake the device writes is created as the format its description states and stored through the one its
        // dispatch declares.
        if (shape.mSource == TextureSource::SpriteBake)
        {
            image.mFormat = writtenAs(shape.mFormat);
            image.mUsage = sWrittenTextureUsage;
            image.mStorageFormat = sWrittenFormat;
        }
        return image;
    }

    std::optional<ImageDescription> Texture::describeCompanion(const ImageShape& shape)
    {
        // A normal map's spread, `NormalSpreadPass`'s: a byte a texel from half the map's sides down to one texel,
        // sampled and written by the pass a level at a time.
        if (shape.mCompanion != TextureCompanion::Spread)
            return std::nullopt;
        const std::uint32_t across = std::max(shape.mWidth / 2, 1u);
        const std::uint32_t down = std::max(shape.mHeight / 2, 1u);
        return ImageDescription{ .mWidth = across,
            .mHeight = down,
            .mFormat = toVulkanFormat(NORMAL_SPREAD_FORMAT),
            .mUsage = sWrittenTextureUsage,
            .mMipLevels = levelsTo1x1(across, down) };
    }

    std::optional<ContentPrice> Texture::price(const Device& device, const ImageShape& shape)
    {
        const std::uint32_t side = std::min(device.getPhysicalDevice().getTextureSide(), Shaders::TEXTURE_SIDE_MOST);
        if (shape.mFormat == TextureFormat::Unnamed || shape.mLevels == 0 || shape.mWidth > side
            || shape.mHeight > side)
            return std::nullopt;
        const VkMemoryRequirements image = Image::requirementsFor(device, describe(shape));
        ContentPrice price{ .mBytes = image.size, .mAlignment = image.alignment };
        if (const std::optional<ImageDescription> companion = describeCompanion(shape))
        {
            const VkMemoryRequirements beside = Image::requirementsFor(device, *companion);
            price.mBytes = alignUp(price.mBytes, beside.alignment) + beside.size;
            price.mAlignment = std::max(price.mAlignment, beside.alignment);
        }
        return price;
    }

    ImageShape Texture::shapeOf(const TextureData& data, const std::uint32_t first)
    {
        assert(first < data.mLevels.size() && "a texture begun past the file's last level");
        const MipLevel& top = data.mLevels[first];
        return ImageShape{ .mSource = TextureSource::File,
            .mFormat = data.mFormat,
            .mWidth = top.mWidth,
            .mHeight = top.mHeight,
            .mLevels = static_cast<std::uint8_t>(data.mLevels.size() - first),
            .mCompanion = data.getCompanion() };
    }

    void Texture::make(const Device& device, const ImageShape& shape, std::string_view name, const TexturePlace& place)
    {
        assert(isEmpty() && "a texture stood over one that stands");
        const ImageDescription image = describe(shape);
        const std::optional<ImageDescription> companion = describeCompanion(shape);
        std::string companionName;
        if constexpr (sDebugNames)
            companionName = std::string(name) + " spread";

        // **At the slot residency reserved**, which a price of this shape sized: the companion after the image.
        if (place.mMemory != nullptr)
        {
            mImage = Image(device, image, *place.mMemory, place.mOffset, name);
            if (companion.has_value())
            {
                const VkMemoryRequirements beside = Image::requirementsFor(device, *companion);
                const VkDeviceSize after = alignUp(place.mOffset + Image::bytesFor(device, image), beside.alignment);
                mCompanion = Image(device, *companion, *place.mMemory, after, companionName);
            }
        }
        else
        {
            mImage = Image(MemoryUse::Essential, device, image, name);
            if (companion.has_value())
                mCompanion = Image(MemoryUse::Essential, device, *companion, companionName);
        }
        mShape = shape;
        mBytes = price(device, shape).value_or(ContentPrice{}).mBytes;
    }

    void Texture::standFile(const Device& device, Batch& batch, TextureArrival& arrival, const TextureData& data,
        const std::uint32_t first, std::string_view name, std::vector<VkBufferImageCopy>& regions,
        const TexturePlace& place)
    {
        assert(first < data.mLevels.size() && "a texture begun past the file's last level");
        const Crash::NoteScope noted("staging the texture \"{}\"", data.mName);

        // Every level in one submit, laid back to back in one staging run, one copy a level out of it — and from the
        // first level on, so a texture held to a smaller side stages none of what it leaves out.
        const auto levels = static_cast<std::uint32_t>(data.mLevels.size()) - first;
        regions.clear();
        regions.reserve(levels);
        std::size_t laid = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const MipLevel& from = data.mLevels[first + level];
            regions.push_back(wholeLevel(laid, level, VkExtent3D{ from.mWidth, from.mHeight, 1 }));
            laid += data.bytesOf(first + level).size();
        }

        make(device, shapeOf(data, first), name, place);
        mWrap = data.mWrap;

        const StagingLend staged = batch.reserve(laid);
        for (std::uint32_t level = 0; level < levels; ++level)
            std::ranges::copy(data.bytesOf(first + level),
                staged.mBytes.begin() + static_cast<std::ptrdiff_t>(regions[level].bufferOffset));
        arrival.upload(staged.mRun, mImage, regions);
        if (hasCompanion())
            arrival.spread(mImage, mCompanion);
    }

    void Texture::standMovedDown(const Device& device, TextureArrival& arrival, const Texture& from,
        const std::uint32_t dropped, std::string_view name, const TexturePlace& place)
    {
        assert(!from.isEmpty() && "a move of a texture that does not stand");
        assert(dropped < from.mImage.getMipLevels() && "a move past the texture's last level");
        make(device, from.mShape.from(static_cast<std::uint8_t>(dropped)), name, place);
        mWrap = from.mWrap;
        arrival.copyLevels(from.mImage, dropped, mImage);
        if (hasCompanion())
            arrival.spread(mImage, mCompanion);
    }

    void Texture::standBake(const Device& device, TextureArrival& arrival, const Texture& source,
        const TextureFormat format, std::string_view name, const TexturePlace& place)
    {
        assert(!source.isEmpty());
        ImageShape shape = source.mShape;
        shape.mSource = TextureSource::SpriteBake;
        shape.mFormat = format;
        shape.mCompanion = TextureCompanion::Neutral;
        make(device, shape, name, place);

        // Clamped, because a bake is one image whose coordinates run edge to edge — what
        // `TextureTable::addSpriteLight` says of its row.
        mWrap = TextureWrap::Clamp;
        arrival.bake(source.mImage, mImage);
    }

    void Texture::standComposite(
        const Device& device, const TextureFormat format, std::string_view name, const TexturePlace& place)
    {
        // A chain to one texel, which the bake's encoder copies in whole.
        Crash::contract(format == TextureFormat::Bc7Srgb || format == TextureFormat::Bc7Unorm,
            "a composite described in a format its encoder does not write");
        constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
        make(device,
            ImageShape{ .mSource
                = format == TextureFormat::Bc7Unorm ? TextureSource::GroundGloss : TextureSource::GroundComposite,
                .mFormat = format,
                .mWidth = extent,
                .mHeight = extent,
                .mLevels = static_cast<std::uint8_t>(levelsTo1x1(extent, extent)) },
            name, place);

        // Clamped, because a composite is one image whose coordinates run edge to edge — what
        // `TextureTable::addGround` says of its row.
        mWrap = TextureWrap::Clamp;
    }

    void Texture::standColour(const Device& device, Batch& batch, TextureArrival& arrival, const std::string_view name,
        const osg::Vec4f& colour)
    {
        assert(isEmpty() && "a texture stood over one that stands");

        mImage = Image(MemoryUse::Essential, device,
            ImageDescription{
                .mWidth = 1, .mHeight = 1, .mFormat = VK_FORMAT_R32G32B32A32_SFLOAT, .mUsage = sUploadedTextureUsage },
            name);

        const std::array<float, 4> texel{ colour.x(), colour.y(), colour.z(), colour.w() };
        const std::array<VkBufferImageCopy, 1> regions{ wholeLevel(0, 0, VkExtent3D{ 1, 1, 1 }) };
        arrival.upload(batch.stage(std::as_bytes(std::span<const float>(texel))), mImage, regions);

        mBytes = priceColour().standing();
    }

    SetLayout TextureArray::describeLayout(const Device& device)
    {
        // Partially bound because a scene with fewer textures than the array can hold leaves the
        // tail unwritten, and the samplers before the first `sync`. Update after bind, because an arrival writes this
        // set while work that named it is still on the queue — legal as long as no pending command reads that
        // descriptor, and a slot nothing has described is a slot no material names.
        constexpr VkDescriptorBindingFlags sBound
            = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        constexpr std::array<VkDescriptorBindingFlags, sBindings.size()> flags{ sBound, sBound };
        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext = nullptr,
            .bindingCount = static_cast<std::uint32_t>(flags.size()),
            .pBindingFlags = flags.data(),
        };

        return makeSetLayout(
            device, sBindings, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT, &bindingFlags);
    }

    TextureArray::TextureArray(const Device& device, Batch& batch, const SetLayout& layout, const TexturePasses& passes,
        TextureArrival& arrival, const std::uint32_t slots, const std::uint32_t anisotropy)
        : mDevice(device)
        , mSamplers{ makeContentSampler(device, "textures repeating", TextureWrap::Repeat),
            makeContentSampler(device, "textures clamped along s", TextureWrap::ClampS),
            makeContentSampler(device, "textures clamped along t", TextureWrap::ClampT),
            makeContentSampler(device, "textures clamped", TextureWrap::Clamp) }
        , mFootprintSamplers(makeFootprintSamplers(device, anisotropy))
        , mSideLimit(std::min(device.getPhysicalDevice().getTextureSide(), Shaders::TEXTURE_SIDE_MOST))
        // Allocated at the maximum the layout declares, not at what this scene brought: the maximum's pool is paid
        // once (`sBindings`), and an operation only ever owes the slots it touched.
        , mSets(device, sBindings, layout.get(), sFrameSlots, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT)
    {
        mNeutral.standColour(device, batch, arrival, "neutral texel",
            osg::Vec4f(
                Shaders::UNTEXTURED_ALBEDO.x(), Shaders::UNTEXTURED_ALBEDO.y(), Shaders::UNTEXTURED_ALBEDO.z(), 1.0f));

        // Essential: what every slot draws that stands nothing yet.
        mStandIn.standFile(device, batch, arrival, describeStandIn(), 0, "stand-in", mRegionScratch, TexturePlace{});
        arrival.record(batch, passes, nullptr);

        // The last slot is the neutral texel's. `TextureTable` refuses a slot past it, so a scene that reaches it is a
        // table that broke that rule.
        Crash::contract(slots <= Shaders::TEXTURE_NEUTRAL, "a scene with more textures than its table may hand out");

        // The neutral texel's count, and nought for every slot nothing stands, which no material names. Owed to every
        // copy and every set from the start: written by the `sync` before the first placement that binds them.
        mTexels.open(device, sFrameSlots, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, "texture texels");
        mTexels.grow(Shaders::TEXTURE_SLOTS);
        mTexels.write(TextureIndex(Shaders::TEXTURE_NEUTRAL)) = 1;
        for (SlotSet<TextureIndex>& owed : mOwed.live())
            owed.addMakingRoom(TextureIndex(Shaders::TEXTURE_NEUTRAL));

        writeSamplers();
        mSlots.resize(slots);
    }

    void TextureArray::reserveSlot(const TextureIndex slot)
    {
        Crash::contract(slot.at() < Shaders::TEXTURE_NEUTRAL, "a texture slot past what its table may hand out");

        // Grown to reach it rather than one at a time: arrivals come in whatever order the scene's
        // free list handed the slots out, so the highest is not always the last.
        mSlots.growTo(slot);
    }

    void TextureArray::execute(const Executing& executing, std::span<const ResidencyOp> ops)
    {
        for (const ResidencyOp& op : ops)
        {
            if (const auto* standing = std::get_if<StandImage>(&op))
                stand(executing, *standing);
            else if (const auto* dropping = std::get_if<DropImage>(&op))
            {
                assert(mSlots.holds(dropping->mTexture) && "a texture dropped from a slot the array never had");
                Slot& slot = mSlots[dropping->mTexture];
                assert(!(slot.mTexture == nullptr) && "a texture dropped that stands nothing");
                replace(slot);
                slot.mStandIn = true;
                count(slot);
                owe(dropping->mTexture);
            }
        }

        // Every source before every bake, as the arrival records them, and what each replaced kept until then.
        executing.mArrival.record(executing.mBatch, executing.mPasses, executing.mTimer);
        mReplaced.clear();
    }

    void TextureArray::replace(Slot& slot)
    {
        forget(slot);
        if (slot.mTexture != nullptr)
            mReplaced.push_back(std::move(slot.mTexture));
    }

    void TextureArray::stand(const Executing& executing, const StandImage& stand)
    {
        reserveSlot(stand.mTexture);
        Slot& slot = mSlots[stand.mTexture];

        // Named only where a capture or a validation message could read it back.
        std::string name;
        if constexpr (sDebugNames)
            name = "texture " + std::to_string(stand.mTexture.at());

        const TexturePlace place{ .mMemory = &executing.mArena.getBlock(stand.mTo.mBlock),
            .mOffset = stand.mTo.mOffset };

        // What it stood is kept until the arrival records: a lowering and a move copy out of it.
        const bool stood = slot.mTexture != nullptr;
        replace(slot);
        const Texture* const was = stood ? mReplaced.back().get() : nullptr;
        // Its own object, whose address the arrival's work names however the array grows or the slot is stood again.
        slot.mTexture = std::make_unique<Texture>();

        if (stand.mTexels == nullptr)
        {
            // A lowering, or a move out of a draining slab: the same image's levels copied on the device.
            assert(was != nullptr && "a texture moved that stands nothing");
            assert(stand.mFrom.has_value() && "a move with no source named");
            assert(stand.mLevel >= stand.mFromLevel && "a move to a finer level than stands");
            slot.mTexture->standMovedDown(
                mDevice, executing.mArrival, *was, stand.mLevel - stand.mFromLevel, name, place);
        }
        else
        {
            const TextureData& texels = *stand.mTexels;
            slot.mSource = texels.mSource;
            switch (texels.mSource)
            {
                case TextureSource::File:
                    // Every level from the one planned: the feed hands a file's whole chain. **Recorded and flushed
                    // where what the files staged reaches the kept size**, between two whole files, so a load's staging
                    // stays bounded: a PBR world's build staged 1.4 GiB in one batch.
                    slot.mTexture->standFile(mDevice, executing.mBatch, executing.mArrival, texels, stand.mLevel, name,
                        mRegionScratch, place);
                    if (executing.mBatch.getStagedBytes() >= sStagingKept)
                    {
                        executing.mArrival.record(executing.mBatch, executing.mPasses, nullptr);
                        executing.mBatch.flush();
                    }
                    break;
                case TextureSource::SpriteBake:
                {
                    const TextureIndex source = std::get<TextureIndex>(texels.mFrom);
                    Crash::contract(mSlots.holds(source) && !(mSlots[source].mTexture == nullptr),
                        "a sprite light bake names a source that does not stand");
                    slot.mTexture->standBake(
                        mDevice, executing.mArrival, *mSlots[source].mTexture, texels.mFormat, name, place);
                    break;
                }
                case TextureSource::GroundComposite:
                case TextureSource::GroundGloss:
                    makeCanvas(texels.mSource);
                    slot.mTexture->standComposite(mDevice, texels.mFormat, name, place);
                    mPendingComposites.push_back(PendingComposite{ .mSlot = stand.mTexture,
                        .mMaterial = std::get<MaterialIndex>(texels.mFrom),
                        .mOutput = texels.mSource == TextureSource::GroundGloss ? Shaders::GROUND_COMPOSITE_GLOSS
                                                                                : Shaders::GROUND_COMPOSITE_ALBEDO,
                        .mReadsAt = mPendingReads.size(),
                        .mReadsCount = texels.mReads.size() });
                    mPendingReads.insert(mPendingReads.end(), texels.mReads.begin(), texels.mReads.end());
                    break;
                case TextureSource::StandIn:
                    Crash::fatal("a stand-in stood rather than drawn as the one the array holds");
            }
        }

        slot.mStandIn = false;
        count(slot);
        owe(stand.mTexture);
    }

    void TextureArray::standIn(std::span<const TextureIndex> taken)
    {
        for (const TextureIndex at : taken)
        {
            reserveSlot(at);
            Slot& slot = mSlots[at];
            if (!(slot.mTexture == nullptr) || slot.mStandIn)
                continue;
            slot.mStandIn = true;
            owe(at);
        }
    }

    void TextureArray::free(std::span<const TextureIndex> freed)
    {
        for (const TextureIndex at : freed)
        {
            // A slot this array never held: a scene can add a texture and free it in the same window.
            if (!mSlots.holds(at))
                continue;
            // What stood was dropped by residency as the row went; the descriptor is left naming what has gone, which
            // `PARTIALLY_BOUND` makes legal for a slot no live material names.
            assert((mSlots[at].mTexture == nullptr) && "a slot freed while its texture stands");
            mSlots[at].mStandIn = false;
        }
    }

    void TextureArray::makeCanvas(const TextureSource source)
    {
        if (!mCanvas.mBlocks.has_value())
        {
            mCanvas.mBlocks.emplace(GroundCompositePass::makeBlocks(mDevice, "composite blocks"));
            mHeld.mCanvasBytes += priceBlocks().standing();
        }
        const bool gloss = source == TextureSource::GroundGloss;
        std::optional<Image>& canvas = gloss ? mCanvas.mGloss : mCanvas.mAlbedo;
        if (!canvas.has_value())
        {
            canvas.emplace(
                GroundCompositePass::makeCanvas(mDevice, gloss, gloss ? "gloss canvas" : "composite canvas"));
            mHeld.mCanvasBytes += priceCanvas().standing();
        }
    }

    void TextureArray::forget(const Slot& slot)
    {
        // Whether it is there and not its size: a slot stands a texture or it does not.
        if ((slot.mTexture == nullptr))
            return;
        assert(mHeld.mCount > 0 && "a slot taken off a count that never counted it");
        assert(mHeld.mBytes >= slot.mTexture->getBytes() && "a slot taken off bytes that never counted it");
        --mHeld.mCount;
        mHeld.mBytes -= slot.mTexture->getBytes();
        bytesBySource(mHeld, slot) -= slot.mTexture->getBytes();
    }

    void TextureArray::count(const Slot& slot)
    {
        if ((slot.mTexture == nullptr))
            return;
        ++mHeld.mCount;
        mHeld.mBytes += slot.mTexture->getBytes();
        bytesBySource(mHeld, slot) += slot.mTexture->getBytes();
    }

    void TextureArray::owe(const TextureIndex at)
    {
        // How many texels the slot now holds, for `coneLod`, the wrap that picks its sampler, and
        // whether they are the stand-in's, which every reader of an optional map asks —
        // `TEXTURE_STANDS_IN`. Owed to every copy beside the descriptor owed to every set.
        const Slot& slot = mSlots[at];
        const Texture& standing = standingIn(slot);
        const std::uint32_t texels = standing.getImage().getWidth() * standing.getImage().getHeight();
        assert(texels <= Shaders::TEXTURE_TEXELS && "a texel count that reaches the wrap");
        mTexels.write(at) = texels | (static_cast<std::uint32_t>(standing.getWrap()) << Shaders::TEXTURE_WRAP_SHIFT)
            | (slot.mStandIn ? Shaders::TEXTURE_STANDS_IN : 0u);

        for (SlotSet<TextureIndex>& owed : mOwed.live())
            owed.addMakingRoom(at);
    }

    void TextureArray::checkReads(const PendingComposite& pending) const
    {
        for (const TextureIndex read : std::span(mPendingReads).subspan(pending.mReadsAt, pending.mReadsCount))
        {
            const bool standsIn = read.at() >= mSlots.size() || mSlots[read].mStandIn || mSlots[read].isEmpty();
            if (!standsIn)
                continue;
            Log(Debug::Warning) << "Ray tracing baked the ground of material " << pending.mMaterial.at()
                                << " over texture slot " << read.at() << ", which draws the stand-in";
            assert(false && "a ground composite baked over a layer that draws the stand-in");
        }
    }

    VkDescriptorSet TextureArray::getSet(const FrameSlot slot) const
    {
        assert(slot.get() < sFrameSlots);
        // A set with descriptors owed names what it no longer holds: after `setAnisotropy`, a
        // sampler destroyed with the ones it replaced.
        assert(mOwed.at(slot).empty() && "a texture set sampled before `sync` paid what it owes");
        mBound.at(slot).nameFor(mDevice.getTimeline().getNext());
        return mSets.get(slot.get());
    }

    void TextureArray::finishReads(const FrameSlot slot) const
    {
        mBound.at(slot).waitIdle(mDevice, "a trace still sampling a texture set");
        mTexels.finishReads(slot);
    }

    void TextureArray::sync(const FrameSlot slot, Uploads& uploads)
    {
        mTexels.sync(slot, uploads);

        SlotSet<TextureIndex>& owed = mOwed.at(slot);
        if (owed.empty())
            return;

        assert(mBound.at(slot).isIdle(mDevice) && "a descriptor written under a submit still bound to it");

        // One write per slot and per array rather than one over a range: the arrivals are wherever
        // the scene's free list put them, and a run is no longer what they are. Reserved before
        // any write points into it, since a write names its image by address.
        const std::span<const TextureIndex> slots = owed.getSlots();
        const std::size_t writes = 2 * slots.size();
        mImageScratch.clear();
        mWriteScratch.clear();
        mImageScratch.reserve(writes);
        mWriteScratch.reserve(writes);

        const VkDescriptorSet set = mSets.get(slot.get());

        for (const TextureIndex at : slots)
        {
            // Owed and since dropped: the slot holds nothing, and a descriptor left naming what has
            // gone is what `drop` says is legal. The neutral texel and the stand-in stand beside
            // the array.
            const Texture& held = at.at() == Shaders::TEXTURE_NEUTRAL ? mNeutral : standingIn(mSlots[at]);
            if (held.isEmpty())
                continue;

            queueWrite(set, Shaders::TEXTURE_BIND_IMAGES, Shaders::textureImageOf(at.at()),
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, held.describe(), mImageScratch, mWriteScratch);
            // A companion only where a read reaches one, a normal map's: the binding is partially bound.
            if (held.hasCompanion())
                queueWrite(set, Shaders::TEXTURE_BIND_IMAGES, Shaders::companionImageOf(at.at()),
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, held.describeCompanion(), mImageScratch, mWriteScratch);
        }

        updateSets(mDevice, mWriteScratch);

        owed.clear();
    }

    bool TextureArray::bakeComposites(const VkCommandBuffer commands, const GroundCompositePass& pass,
        const FrameSlot slot, const Shaders::GpuTables& tables, GpuTimer* const timer)
    {
        if (mPendingComposites.empty())
            return false;

        const GpuZone timed(timer, commands, FrameZone::Ground);

        // A chunk's two images next to each other, so the one sum writes both: they arrive as two
        // textures, in one hand-over or in two.
        std::sort(mPendingComposites.begin(), mPendingComposites.end(),
            [](const PendingComposite& a, const PendingComposite& b) { return a.mMaterial < b.mMaterial; });

        bool baked = false;
        const VkDescriptorSet set = getSet(slot);
        for (std::size_t at = 0; at < mPendingComposites.size();)
        {
            const MaterialIndex material = mPendingComposites[at].mMaterial;
            const Image* albedo = nullptr;
            const Image* gloss = nullptr;
            std::uint32_t outputs = 0;
            // Up to one of each: a chunk queued twice over before a placement baked it is two sums,
            // which is what it was before the pair shared one.
            for (; at < mPendingComposites.size() && mPendingComposites[at].mMaterial == material
                && (outputs & mPendingComposites[at].mOutput) == 0;
                ++at)
            {
                // Arrived and since dropped, before any placement baked it: the slot holds nothing,
                // and a bake of nothing is nothing to record.
                const PendingComposite& pending = mPendingComposites[at];
                if (mSlots[pending.mSlot].mTexture == nullptr)
                    continue;
                const Texture& held = *mSlots[pending.mSlot].mTexture;

                checkReads(pending);
                (pending.mOutput == Shaders::GROUND_COMPOSITE_GLOSS ? gloss : albedo) = &held.getImage();
                outputs |= pending.mOutput;
            }

            if (outputs == 0)
                continue;

            assert((albedo == nullptr || mCanvas.mAlbedo.has_value())
                && (gloss == nullptr || mCanvas.mGloss.has_value()) && mCanvas.mBlocks.has_value()
                && "a composite pending with no canvas to bake it on");
            pass.record(commands, set, albedo != nullptr ? &*mCanvas.mAlbedo : nullptr,
                gloss != nullptr ? &*mCanvas.mGloss : nullptr,
                Shaders::GroundCompositeConstants{
                    .mMaterials = tables.mMaterials,
                    .mLayers = tables.mLayers,
                    .mMasks = tables.mMasks,
                    .mMaterial = materialRecordOf(material).at(),
                    .mOutputs = outputs,
                    .mTexels = tables.mTextureTexels,
                });
            if (albedo != nullptr)
                pass.encode(commands, *mCanvas.mAlbedo, *mCanvas.mBlocks, *albedo);
            if (gloss != nullptr)
                pass.encode(commands, *mCanvas.mGloss, *mCanvas.mBlocks, *gloss);
            baked = true;
        }

        mPendingComposites.clear();
        mPendingReads.clear();
        return baked;
    }

    void TextureArray::setAnisotropy(const std::uint32_t anisotropy)
    {
        // The samplers replaced bury themselves, and every set names the new ones before this returns: a
        // set left naming the old until its next placement was a picture's scene traced unplaced through
        // samplers the graveyard had destroyed.
        mFootprintSamplers = makeFootprintSamplers(mDevice, anisotropy);
        writeSamplers();
    }

    void TextureArray::writeSamplers()
    {
        mImageScratch.clear();
        mWriteScratch.clear();
        mImageScratch.reserve(mSets.getCount() * Shaders::TEXTURE_SAMPLERS);
        mWriteScratch.reserve(mSets.getCount() * Shaders::TEXTURE_SAMPLERS);
        for (std::uint32_t copy = 0; copy < mSets.getCount(); ++copy)
        {
            assert((copy >= mBound.live().size() || mBound.live()[copy].isIdle(mDevice))
                && "samplers written under a submit still bound to their set");
            const VkDescriptorSet set = mSets.get(copy);
            for (std::size_t wrap = 0; wrap < sTextureWrapCount; ++wrap)
            {
                const auto at = static_cast<std::uint32_t>(wrap);
                queueWrite(set, Shaders::TEXTURE_BIND_SAMPLERS, at, VK_DESCRIPTOR_TYPE_SAMPLER,
                    describeSampler(mSamplers[wrap].get()), mImageScratch, mWriteScratch);
                queueWrite(set, Shaders::TEXTURE_BIND_SAMPLERS, Shaders::TEXTURE_SAMPLERS_ALONG + at,
                    VK_DESCRIPTOR_TYPE_SAMPLER, describeSampler(mFootprintSamplers[wrap].get()), mImageScratch,
                    mWriteScratch);
            }
        }
        updateSets(mDevice, mWriteScratch);
    }

    void TextureArray::queueWrite(const VkDescriptorSet set, const std::uint32_t binding, const std::uint32_t element,
        const VkDescriptorType type, const VkDescriptorImageInfo& image, std::vector<VkDescriptorImageInfo>& images,
        std::vector<VkWriteDescriptorSet>& writes)
    {
        // Each write names its image by address, so a push past the reserve, which moves the images,
        // leaves every earlier write pointing at freed memory.
        assert(images.size() < images.capacity() && "a descriptor write past what its writes reserved");
        images.push_back(image);
        writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = set,
            .dstBinding = binding,
            .dstArrayElement = element,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = &images.back(),
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        });
    }

    Shaders::uvec2 TextureArray::getExtent(const TextureIndex slot) const
    {
        assert(mSlots.holds(slot) && "the extent of a slot the array does not have");
        const Image& image = standingIn(mSlots[slot]).getImage();
        return Shaders::uvec2(image.getWidth(), image.getHeight());
    }

    const Image& TextureArray::getImage(const TextureIndex slot) const
    {
        assert(mSlots.holds(slot) && "the image of a slot the array does not have");
        return standingIn(mSlots[slot]).getImage();
    }

    std::uint64_t& TextureArray::bytesBySource(TexturesHeld& held, const Slot& slot)
    {
        switch (slot.mSource)
        {
            case TextureSource::File:
                return held.mBySource.mFiles;
            case TextureSource::SpriteBake:
                return held.mBySource.mSpriteBakes;
            case TextureSource::GroundComposite:
                return held.mBySource.mGroundComposites;
            case TextureSource::GroundGloss:
                return held.mBySource.mGroundGloss;
            case TextureSource::StandIn:
                break;
        }
        Crash::fatal("a stand-in's slot counted as a texture of its own");
    }
}
