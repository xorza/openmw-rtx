#include "texture.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
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
        /// A normal map's spread, `NormalSpreadPass`'s: a byte a texel from half the map's `width` by
        /// `height` down to one texel, sampled and written by the pass a level at a time.
        Result<Image, MemoryRefusal> makeSpreadMap(
            const Device& device, std::string_view name, MemoryUse use, std::uint32_t width, std::uint32_t height)
        {
            std::string spreadName;
            if constexpr (sDebugNames)
                spreadName = std::string(name) + " spread";

            const std::uint32_t across = std::max(width / 2, 1u);
            const std::uint32_t down = std::max(height / 2, 1u);
            return Image::tryMake(use, device,
                ImageDescription{ .mWidth = across,
                    .mHeight = down,
                    .mFormat = toVulkanFormat(NORMAL_SPREAD_FORMAT),
                    .mUsage = sWrittenTextureUsage,
                    .mMipLevels = levelsTo1x1(across, down) },
                spreadName);
        }

        /// Whether `texture` stands in `block` of content's pools, its image or its companion: what moves to give the
        /// block back. An empty texture stands nowhere.
        bool standsIn(const Texture& texture, const VkDeviceMemory block)
        {
            const auto in = [block](const Image& image) {
                return image.getMemory().standsInContentBlock() && image.getMemory().getHandle() == block;
            };
            return in(texture.getImage()) || in(texture.getCompanion());
        }

        ImageShape shapeOf(const Image& image)
        {
            return ImageShape{
                .mWidth = image.getWidth(), .mHeight = image.getHeight(), .mLevels = image.getMipLevels()
            };
        }

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
        /// set's two image arrays are 1 MB of pool.
        constexpr std::array<VkDescriptorSetLayoutBinding, 3> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::TEXTURE_BIND_IMAGES, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                Shaders::TEXTURE_SLOTS, sStages, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::TEXTURE_BIND_COMPANIONS, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                Shaders::TEXTURE_SLOTS, sStages, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::TEXTURE_BIND_SAMPLERS, VK_DESCRIPTOR_TYPE_SAMPLER,
                Shaders::TEXTURE_SAMPLERS, sStages, nullptr }
        };

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

        /// Why a texture draws the stand-in where the device takes none of its levels.
        std::string pastTheSide(const TextureData& texture, std::uint32_t limit)
        {
            const MipLevel& last = texture.mLevels.back();
            return "its smallest level is " + std::to_string(last.mWidth) + " by " + std::to_string(last.mHeight)
                + " texels, and the device takes " + std::to_string(limit) + " a side";
        }
    }

    Result<void, MemoryRefusal> Texture::standFile(const Device& device, Batch& batch, TextureArrival& arrival,
        const TextureData& data, const std::uint32_t first, std::string_view name,
        std::vector<VkBufferImageCopy>& regions, const MemoryUse use)
    {
        assert(isEmpty() && "a texture stood over one that stands");
        assert(first < data.mLevels.size() && "a texture begun past the file's last level");

        const Crash::NoteScope noted("staging the texture \"{}\"", data.mName);

        const MipLevel& top = data.mLevels[first];
        const auto levels = static_cast<std::uint32_t>(data.mLevels.size()) - first;

        // Every level in one submit, laid back to back in one staging run, one copy a level out of
        // it — and from the first level on, so a texture held to a smaller side stages none of what
        // it leaves out.
        regions.clear();
        regions.reserve(levels);
        std::size_t laid = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const MipLevel& from = data.mLevels[first + level];
            regions.push_back(wholeLevel(laid, level, VkExtent3D{ from.mWidth, from.mHeight, 1 }));
            laid += data.bytesOf(first + level).size();
        }

        // A normal map's spread, measured by a dispatch behind the texture through the arrival's means
        // (`TextureData::getCompanion`); nothing beside any other texture, whose companion no read
        // reaches.
        const bool spread = data.getCompanion() == TextureCompanion::Spread;
        Result<Image, MemoryRefusal> companion
            = spread ? makeSpreadMap(device, name, use, top.mWidth, top.mHeight) : Image();
        if (!companion.isOk())
            return Err{ companion.error() };

        Result<Image, MemoryRefusal> image = Image::tryMake(use, device, describeFile(data, first), name);
        if (!image.isOk())
            return Err{ image.error() };

        // Every image is made, so nothing below can refuse: the texture stands from here, and what
        // makes it is the arrival's to record.
        mWrap = data.mWrap;
        mImage = std::move(image.value());
        mCompanion = std::move(companion.value());

        const StagingLend staged = batch.reserve(laid);
        for (std::uint32_t level = 0; level < levels; ++level)
            std::ranges::copy(data.bytesOf(first + level),
                staged.mBytes.begin() + static_cast<std::ptrdiff_t>(regions[level].bufferOffset));
        arrival.upload(staged.mRun, mImage, regions);

        if (spread)
            arrival.spread(mImage, mCompanion);

        mBytes = priceFile(data, first).standing();
        mFormat = data.mFormat;
        return {};
    }

    Result<void, MemoryRefusal> Texture::standMovedDown(const Device& device, TextureArrival& arrival,
        const Texture& from, const std::uint32_t dropped, std::string_view name, const MemoryUse use)
    {
        assert(isEmpty() && "a texture stood over one that stands");
        assert(!from.isEmpty() && "a move of a texture that does not stand");
        assert(dropped > 0 && dropped < from.mImage.getMipLevels() && "a move past the texture's last level");

        const ImageShape shape{ .mWidth = from.mImage.getWidthAt(dropped),
            .mHeight = from.mImage.getHeightAt(dropped),
            .mLevels = from.mImage.getMipLevels() - dropped };
        const bool spread = from.hasCompanion();
        Result<Image, MemoryRefusal> companion
            = spread ? makeSpreadMap(device, name, use, shape.mWidth, shape.mHeight) : Image();
        if (!companion.isOk())
            return Err{ companion.error() };

        Result<Image, MemoryRefusal> image = Image::tryMake(use, device,
            ImageDescription{ .mWidth = shape.mWidth,
                .mHeight = shape.mHeight,
                .mFormat = from.mImage.getFormat(),
                .mUsage = sUploadedTextureUsage,
                .mMipLevels = shape.mLevels },
            name);
        if (!image.isOk())
            return Err{ image.error() };

        mWrap = from.mWrap;
        mFormat = from.mFormat;
        mImage = std::move(image.value());
        mCompanion = std::move(companion.value());
        arrival.copyLevels(from.mImage, dropped, mImage);
        if (spread)
            arrival.spread(mImage, mCompanion);
        mBytes = priceShape(mFormat, shape, spread).standing();
        return {};
    }

    Result<void, MemoryRefusal> Texture::standMovedOut(const Device& device, TextureArrival& arrival,
        const Texture& from, const VkDeviceMemory block, std::string_view name)
    {
        assert(isEmpty() && "a texture stood over one that stands");
        assert(!from.isEmpty() && "a move of a texture that does not stand");

        Result<Image, MemoryRefusal> companion = from.hasCompanion()
            ? Image::tryMakeOutside(device, from.mCompanion.getDescription(), block, name)
            : Image();
        if (!companion.isOk())
            return Err{ companion.error() };
        Result<Image, MemoryRefusal> image = Image::tryMakeOutside(device, from.mImage.getDescription(), block, name);
        if (!image.isOk())
            return Err{ image.error() };

        mWrap = from.mWrap;
        mFormat = from.mFormat;
        mBytes = from.mBytes;
        mImage = std::move(image.value());
        mCompanion = std::move(companion.value());
        arrival.copyLevels(from.mImage, 0, mImage);
        if (from.hasCompanion())
            arrival.copyLevels(from.mCompanion, 0, mCompanion);
        return {};
    }

    ImageDescription Texture::describeFile(const TextureData& data, const std::uint32_t first)
    {
        const MipLevel& top = data.mLevels[first];
        return ImageDescription{ .mWidth = top.mWidth,
            .mHeight = top.mHeight,
            .mFormat = toVulkanFormat(data.mFormat),
            .mUsage = sUploadedTextureUsage,
            .mMipLevels = static_cast<std::uint32_t>(data.mLevels.size()) - first };
    }

    Result<void, MemoryRefusal> Texture::standBake(const Device& device, TextureArrival& arrival, const Texture& source,
        const TextureFormat format, std::string_view name)
    {
        assert(isEmpty() && "a texture stood over one that stands");
        assert(!source.isEmpty());

        const Image& from = source.mImage;
        Result<Image, MemoryRefusal> image = Image::tryMake(MemoryUse::Texture, device,
            ImageDescription{ .mWidth = from.getWidth(),
                .mHeight = from.getHeight(),
                .mFormat = writtenAs(format),
                .mUsage = sWrittenTextureUsage,
                .mMipLevels = from.getMipLevels(),
                .mStorageFormat = sWrittenFormat },
            name);
        if (!image.isOk())
            return Err{ image.error() };

        mImage = std::move(image.value());

        // Clamped, because a bake is one image whose coordinates run edge to edge — what
        // `TextureTable::addSpriteLight` says of its row.
        mWrap = TextureWrap::Clamp;

        arrival.bake(from, mImage);

        mBytes = priceBake(shapeOf(mImage)).standing();
        return {};
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

    Result<void, MemoryRefusal> Texture::standComposite(
        const Device& device, TextureArrival& arrival, const TextureFormat format, std::string_view name)
    {
        assert(isEmpty() && "a texture stood over one that stands");

        // A chain to one texel, which the bake's encoder copies in whole.
        Crash::contract(format == TextureFormat::Bc7Srgb || format == TextureFormat::Bc7Unorm,
            "a composite described in a format its encoder does not write");
        constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
        Result<Image, MemoryRefusal> image = Image::tryMake(MemoryUse::Texture, device,
            ImageDescription{ .mWidth = extent,
                .mHeight = extent,
                .mFormat = toVulkanFormat(format),
                .mUsage = sUploadedTextureUsage,
                .mMipLevels = levelsTo1x1(extent, extent) },
            name);
        if (!image.isOk())
            return Err{ image.error() };

        mImage = std::move(image.value());

        // Clamped, because a composite is one image whose coordinates run edge to edge — what
        // `TextureTable::addGround` says of its row.
        mWrap = TextureWrap::Clamp;

        mBytes = priceComposite().standing();
        return {};
    }

    SetLayout TextureArray::describeLayout(const Device& device)
    {
        // Partially bound because a scene with fewer textures than the array can hold leaves the
        // tail unwritten, and the samplers before the first `sync`. Update after bind, because an arrival writes this
        // set while work that named it is still on the queue — legal as long as no pending command reads that
        // descriptor, and a slot nothing has described is a slot no material names.
        constexpr VkDescriptorBindingFlags sBound
            = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        constexpr std::array<VkDescriptorBindingFlags, 3> flags{ sBound, sBound, sBound };
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
        , mRefusedSeen(device.getMemory().getRefusedCount())
        , mSamplers{ makeContentSampler(device, "textures repeating", TextureWrap::Repeat),
            makeContentSampler(device, "textures clamped along s", TextureWrap::ClampS),
            makeContentSampler(device, "textures clamped along t", TextureWrap::ClampT),
            makeContentSampler(device, "textures clamped", TextureWrap::Clamp) }
        , mFootprintSamplers(makeFootprintSamplers(device, anisotropy))
        , mSideLimit(std::min(device.getPhysicalDevice().getTextureSide(), Shaders::TEXTURE_SIDE_MOST))
        // Allocated at the maximum the layout declares, not at what this scene brought. Sizing the
        // set to the cell is what made a texture arriving mean a new set, a new pool and every
        // image uploaded again; the maximum's pool is paid once (`sBindings`). `write` then only
        // ever owes the slots that are new.
        , mSets(device, sBindings, layout.get(), sFrameSlots, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT)
    {
        mNeutral.standColour(device, batch, arrival, "neutral texel",
            osg::Vec4f(
                Shaders::UNTEXTURED_ALBEDO.x(), Shaders::UNTEXTURED_ALBEDO.y(), Shaders::UNTEXTURED_ALBEDO.z(), 1.0f));

        // Essential, and the one texture made that way: it is what stands where the device had no
        // room for the texture itself.
        [[maybe_unused]] const Result<void, MemoryRefusal> stood = mStandIn.standFile(
            device, batch, arrival, describeStandIn(), 0, "stand-in", mRegionScratch, MemoryUse::Essential);
        assert(stood.isOk() && "essential memory refused rather than thrown");
        arrival.record(batch, passes, nullptr);

        // The last slot is the neutral texel's. `TextureTable` refuses a slot past it, so a scene
        // that reaches it is a table that broke that rule.
        Crash::contract(slots <= Shaders::TEXTURE_NEUTRAL, "a scene with more textures than its table may hand out");

        // The neutral texel's count, and nought for every slot nothing stands, which no material
        // names. Owed to every copy and every set from the start, the way an arrival is: written by
        // the `sync` before the first placement that binds them.
        mTexels.open(device, sFrameSlots, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, "texture texels");
        mTexels.grow(Shaders::TEXTURE_SLOTS);
        mTexels.write(TextureIndex(Shaders::TEXTURE_NEUTRAL)) = 1;
        for (SlotSet<TextureIndex>& owed : mOwed.live())
            owed.addMakingRoom(TextureIndex(Shaders::TEXTURE_NEUTRAL));

        writeSamplers();

        // Sized to the table before anything is written into it, so a description lands in the
        // slot it names whatever sits either side of it. Every entry starts holding no image and no
        // map, which is what a free slot goes on holding: its descriptors are never written, and
        // the bindings' `PARTIALLY_BOUND` is what makes that legal for one nothing samples.
        mSlots.resize(slots);
    }

    void TextureArray::reserveSlot(const TextureIndex slot)
    {
        Crash::contract(slot.at() < Shaders::TEXTURE_NEUTRAL, "a texture slot past what its table may hand out");

        // Grown to reach it rather than one at a time: arrivals come in whatever order the scene's
        // free list handed the slots out, so the highest is not always the last.
        mSlots.growTo(slot);
    }

    void TextureArray::write(Batch& batch, const TexturePasses& passes, TextureArrival& arrival,
        std::span<const TextureData> arrived, const TextureTable& table, DeviceAnswer& answer,
        const WhenRoomless whenRoomless, GpuTimer* const timer)
    {
        if (arrived.empty())
            return;

        // Every slot reserved before any stands, so the array does not move a texture the arrival's
        // work names before it records.
        for (const TextureData& texture : arrived)
            reserveSlot(texture.mSlot);

        // What the last write moved out of: the batch that copied from it has gone to the queue since.
        mMovedFrom.clear();

        // **Room made by what stands moving down**, for an arrival the room does not hold whole and for what content
        // holds past its ceiling, a budget that fell beside it: what moves gives its room back once the frames that
        // sample it are done, and the arrival stood short meanwhile is handed over whole again then (`SceneUploader`).
        const MemoryAllocator& memory = mDevice.getMemory();
        const VkDeviceSize room = memory.getRoom(MemoryUse::Texture);
        mFullFirsts.assign(arrived.size(), 0);
        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::File)
                mFullFirsts[at] = arrived[at].topLevel(mSideLimit).value_or(0);
        const VkDeviceSize whole = costAt(arrived, mFullFirsts, true, arrival);
        VkDeviceSize gaveBack = 0;
        if (const VkDeviceSize deficit = (whole > room ? whole - room : 0) + memory.getContentOver(); deficit > 0)
            gaveBack = makeRoom(arrival, RoomAsk{ .mBytes = deficit, .mKeep = arrived }, table, answer);
        chooseLevels(arrived, room, arrival, mFirsts);

        std::uint32_t held = 0;
        std::uint32_t deepest = 0;
        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::File)
                if (const std::optional<std::uint32_t> top = arrived[at].topLevel(mSideLimit); top.has_value())
                {
                    held += mFirsts[at] > *top ? 1 : 0;
                    deepest = std::max(deepest, mFirsts[at] - *top);
                }
        mRoomHeld = held;

        // Said for a drop deeper than any said before, and not per arrival: a device short of room
        // holds every arrival about as far down, and the log would say it at every crossing.
        if (deepest > mSaidDrop)
        {
            Log(Debug::Warning) << "Ray tracing holds " << held << " of " << arrived.size()
                                << " textures that arrive up to " << deepest
                                << " levels below what they come at, which is what the device has room for";
            mSaidDrop = deepest;
        }

        // What gave room back up front leaves it coming for whatever the device still refuses below.
        const Standing standing{ .mBatch = batch,
            .mArrival = arrival,
            .mTable = table,
            .mArrived = arrived,
            .mAnswer = answer,
            .mWhenRoomless = gaveBack > 0 ? WhenRoomless::Waits : whenRoomless };

        // Every source before every bake, because a bake is made from the texture standing in the
        // slot it names, and the ground last, because it is what gives way where the room runs out
        // — `chooseLevels` says why. Three walks and not a sort: the order is the kind's, and
        // `arrived` is what one cell brought.
        //
        // **Recorded and flushed where what the files staged reaches the kept size**, between two whole files, so a
        // load's staging stays bounded: a PBR world's build staged 1.4 GiB in one batch. Untimed, as one frame opens
        // each zone once and the last record is timed.
        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::File || arrived[at].mSource == TextureSource::StandIn)
            {
                stand(standing, arrived[at], mFirsts[at]);
                if (batch.getStagedBytes() >= sStagingKept)
                {
                    arrival.record(batch, passes, nullptr);
                    batch.flush();
                }
            }

        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::SpriteBake)
                stand(standing, arrived[at], mFirsts[at]);

        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::GroundComposite
                || arrived[at].mSource == TextureSource::GroundGloss)
                stand(standing, arrived[at], mFirsts[at]);

        arrival.record(batch, passes, timer);
    }

    void TextureArray::chooseLevels(std::span<const TextureData> arrived, const VkDeviceSize room,
        const TextureArrival& arrival, std::vector<std::uint32_t>& firsts)
    {
        // What every `costAt` below asks of a bake's source, found once for the arrival.
        mArrivingAt.clear();
        for (std::size_t at = 0; at < arrived.size(); ++at)
        {
            const TextureIndex slot = arrived[at].mSlot;
            mArrivingAt.growTo(slot, sNotArriving);
            mArrivingAt[slot] = static_cast<std::uint32_t>(at);
        }

        // Every file from its first level within the device's side, which a file past the side at
        // every level has none of: it draws the stand-in. **The whole arrival there, where it fits
        // there**, which is every arrival on a card with room, priced once.
        firsts.assign(arrived.size(), 0);
        for (std::size_t at = 0; at < arrived.size(); ++at)
            if (arrived[at].mSource == TextureSource::File)
                firsts[at] = arrived[at].topLevel(mSideLimit).value_or(0);
        if (costAt(arrived, firsts, true, arrival) <= room)
            return;

        // Every level under it a file can come down by.
        mDrops.clear();
        for (std::size_t at = 0; at < arrived.size(); ++at)
        {
            const TextureData& texture = arrived[at];
            if (texture.mSource != TextureSource::File || !texture.topLevel(mSideLimit).has_value())
                continue;
            for (std::uint32_t level = firsts[at]; level + 1 < texture.mLevels.size(); ++level)
                mDrops.push_back(
                    Drop{ .mStanding = priceFile(texture, level).standing(), .mSlot = texture.mSlot, .mAt = at });
        }

        // The file that stands the most bytes comes down a level first, and of two that stand alike the one in the
        // later slot, until the arrival fits: Unreal's streamer takes one mip at a time off the textures it least
        // needs, where one side for the whole arrival came down by halves and left up to three quarters of the room
        // unused. What stands follows from the textures and the room alone, so the same world in the same room stands
        // the same. Each level of a file stands fewer bytes than the one above, so any prefix holds each file's first.
        std::ranges::sort(mDrops, [](const Drop& left, const Drop& right) {
            return std::tie(left.mStanding, left.mSlot) > std::tie(right.mStanding, right.mSlot);
        });

        const auto costAfter = [&](const std::size_t drops, const bool ground) {
            mTrialFirsts.assign(firsts.begin(), firsts.end());
            for (std::size_t drop = 0; drop < drops; ++drop)
                ++mTrialFirsts[mDrops[drop].mAt];
            return costAt(arrived, mTrialFirsts, ground, arrival);
        };

        // The fewest drops that fit, found by halves: every drop takes bytes off and none puts any
        // back, so the cost only falls along the order. Nothing where all of them leave too much.
        const auto fewestFitting = [&](const bool ground) -> std::optional<std::size_t> {
            if (costAfter(mDrops.size(), ground) > room)
                return std::nullopt;
            std::size_t low = 0;
            std::size_t high = mDrops.size();
            while (low < high)
            {
                const std::size_t middle = low + (high - low) / 2;
                if (costAfter(middle, ground) <= room)
                    high = middle;
                else
                    low = middle + 1;
            }
            return low;
        };

        // The whole arrival, where it fits after some drops. Where it fits after none, the ground's composites, which
        // no drop brings down, are more than the room by themselves, and holding the files to their last levels would
        // buy them nothing: the files come down as far as they fit alone, and the ground takes what they leave. What is
        // nearest the eye keeps its detail. Where the files fit after no count of drops either, each comes down as far
        // as its file goes.
        std::optional<std::size_t> drops = fewestFitting(true);
        if (!drops.has_value())
            drops = fewestFitting(false);
        const std::size_t taken = drops.value_or(mDrops.size());
        for (std::size_t drop = 0; drop < taken; ++drop)
            ++firsts[mDrops[drop].mAt];
    }

    VkDeviceSize TextureArray::costAt(std::span<const TextureData> arrived, std::span<const std::uint32_t> firsts,
        const bool ground, const TextureArrival& arrival) const
    {
        // A bake is shaped like its source as the source is held in `firsts`: from its first level
        // where it arrives beside the bake, as the stand-in where it arrives as one, and as its
        // image where it stands already.
        const auto bakeOf = [&](const TextureIndex slot) -> VkDeviceSize {
            const std::uint32_t arrivingAt = mArrivingAt.holds(slot) ? mArrivingAt[slot] : sNotArriving;
            const TextureData* const arriving = arrivingAt != sNotArriving ? &arrived[arrivingAt] : nullptr;

            const Image* standing = nullptr;
            if (arriving != nullptr)
            {
                if (arriving->mSource == TextureSource::File && arriving->topLevel(mSideLimit).has_value())
                    return priceBake(shapeOf(*arriving, firsts[arrivingAt])).standing();

                standing = &mStandIn.getImage();
            }
            else if (mSlots.holds(slot))
                standing = &standingIn(mSlots[slot]).getImage();

            if (standing == nullptr || standing->isEmpty())
                return 0;

            return priceBake(shapeOf(*standing)).standing();
        };

        VkDeviceSize cost = 0;
        VkDeviceSize means = 0;
        VkDeviceSize largestMeans = 0;
        bool blocks = mCanvas.mBlocks.has_value();
        bool albedoCanvas = mCanvas.mAlbedo.has_value();
        bool glossCanvas = mCanvas.mGloss.has_value();
        for (std::size_t at = 0; at < arrived.size(); ++at)
        {
            const TextureData& texture = arrived[at];
            switch (texture.mSource)
            {
                case TextureSource::File:
                    if (texture.topLevel(mSideLimit).has_value())
                    {
                        const std::uint32_t first = firsts[at];
                        cost += priceFile(texture, first).standing();
                        const VkDeviceSize itsMeans = spreadMeansBytes(texture, first);
                        means += itsMeans;
                        largestMeans = std::max(largestMeans, itsMeans);
                    }
                    break;

                case TextureSource::SpriteBake:
                    cost += bakeOf(std::get<TextureIndex>(texture.mFrom));
                    break;

                case TextureSource::GroundComposite:
                case TextureSource::GroundGloss:
                    if (!ground)
                        break;
                    {
                        cost += priceComposite().standing();

                        // What the scene's first composite of each kind is made with, once.
                        bool& canvas = texture.mSource == TextureSource::GroundGloss ? glossCanvas : albedoCanvas;
                        cost += (canvas ? 0 : priceCanvas().standing()) + (blocks ? 0 : priceBlocks().standing());
                        canvas = true;
                        blocks = true;
                        break;
                    }

                case TextureSource::StandIn:
                    break;
            }
        }

        // The means the spreads are built through are the arrival's room, grown once to its largest
        // group and kept: what this arrival needs past what it holds already.
        const VkDeviceSize needed = arrival.meansNeeded(means, largestMeans);
        const VkDeviceSize held = arrival.getMeansBytes();
        return cost + (needed > held ? needed - held : 0);
    }

    Result<void, MemoryRefusal> TextureArray::make(Batch& batch, TextureArrival& arrival, Texture& into,
        const TextureData& texture, const std::uint32_t first, std::string_view name)
    {
        switch (texture.mSource)
        {
            case TextureSource::GroundComposite:
            case TextureSource::GroundGloss:
            {
                const Result<void, MemoryRefusal> canvas = makeCanvas(texture.mSource);
                if (!canvas.isOk())
                    return canvas;
                return into.standComposite(mDevice, arrival, texture.mFormat, name);
            }

            case TextureSource::SpriteBake:
            {
                // The source stands, or draws the stand-in: `SceneTextures` names one only where the table holds it
                // live, and `write` stands every source ahead of every bake. A bake of a slot that holds nothing is a
                // contract broken and not content. A source drawing the stand-in is baked as the stand-in, as it is
                // drawn.
                const TextureIndex source = std::get<TextureIndex>(texture.mFrom);
                Crash::contract(mSlots.holds(source) && !mSlots[source].isEmpty(),
                    "a sprite light bake names a source that does not stand");
                return into.standBake(mDevice, arrival, standingIn(mSlots[source]), texture.mFormat, name);
            }

            case TextureSource::File:
            {
                // From the level `chooseLevels` chose, and a level further down each time the device
                // has no room.
                const auto last = static_cast<std::uint32_t>(texture.mLevels.size()) - 1;
                std::uint32_t level = first;
                Result<void, MemoryRefusal> stood
                    = into.standFile(mDevice, batch, arrival, texture, level, name, mRegionScratch, MemoryUse::Texture);
                while (!stood.isOk() && level < last)
                    stood = into.standFile(
                        mDevice, batch, arrival, texture, ++level, name, mRegionScratch, MemoryUse::Texture);

                return stood;
            }

            case TextureSource::StandIn:
                break;
        }

        Crash::fatal("a stand-in made rather than drawn as the one the array holds");
    }

    RoomWant TextureArray::wantOf(const TextureData& texture) const
    {
        const std::optional<std::uint32_t> top = texture.topLevel(mSideLimit);
        assert(top.has_value() && "a file past the device's side waits on no room");
        const VkMemoryRequirements whole = Image::requirementsFor(mDevice, Texture::describeFile(texture, *top));
        return RoomWant{ .mWaiter = texture.mSlot, .mHole = whole.size, .mAlignment = whole.alignment };
    }

    void TextureArray::stand(const Standing& standing, const TextureData& texture, const std::uint32_t first)
    {
        Batch& batch = standing.mBatch;
        TextureArrival& arrival = standing.mArrival;
        DeviceAnswer& answer = standing.mAnswer;
        assert(mSlots.holds(texture.mSlot) && "a texture stood in a slot `write` did not reserve");
        Slot& slot = mSlots[texture.mSlot];

        // Named only where a capture or a validation message could read it back. A local,
        // because a slot number is short enough that this never reaches the heap; the one that
        // does is the map's, inside `Texture`.
        std::string name;
        if constexpr (sDebugNames)
            name = "texture " + std::to_string(texture.mSlot.at());

        // Why the slot draws the stand-in, or nothing where the texture stands. A file the device takes at no level is
        // known before anything is made; room is known by asking for it. What the slot held is the descriptor a frame
        // in flight bound, and its images bury themselves, so it stays valid until the timeline says nothing reads it.
        // Let go before the new one is made, because the new one is made where it stays: the arrival's work names its
        // images.
        forget(slot);
        slot.mTexture = Texture();
        slot.mShortOfRoom = false;

        std::string why;
        if (texture.mSource == TextureSource::File && !texture.topLevel(mSideLimit).has_value())
            why = pastTheSide(texture, mSideLimit);
        else if (texture.mSource != TextureSource::StandIn)
        {
            // Every refusal `make` answers is the device's room: a file past the side is the one
            // that is not, and it is known above.
            const Result<void, MemoryRefusal> stood = make(batch, arrival, slot.mTexture, texture, first, name);
            slot.mFirst = stood.isOk() ? first : 0;
            if (!stood.isOk())
            {
                slot.mShortOfRoom = true;
                // A file the core can hand over again once the room is back: its slot draws the stand-in meanwhile.
                // **Room is coming where what stands moves down for it**, though nothing rests and nothing is buried:
                // the handheld's PBR tours drew a file's stand-in for good at Seyda Neen where the textures that stood
                // could have come down for it.
                const bool coming = texture.mSource == TextureSource::File
                    && (standing.mWhenRoomless == WhenRoomless::Waits
                        || makeRoom(arrival, RoomAsk{ .mBytes = wantOf(texture).mHole, .mKeep = standing.mArrived },
                               standing.mTable, answer)
                            > 0);
                if (coming)
                {
                    answer.mShort.push_back(ShortTexture{ .mSlot = texture.mSlot, .mStoodBytes = 0 });
                    answer.mWants.push_back(wantOf(texture));
                }
                else
                    why = describe(stood.error());
            }
        }

        const Texture& made = slot.mTexture;
        slot.mStandIn = made.isEmpty();
        slot.mSource = texture.mSource;
        slot.mWholeBytes = 0;
        if (texture.mSource == TextureSource::File)
            if (const std::optional<std::uint32_t> top = texture.topLevel(mSideLimit); top.has_value())
                slot.mWholeBytes = priceFile(texture, *top).standing();
        slot.mReduced = texture.mSource == TextureSource::File && !made.isEmpty()
            && (made.getImage().getWidth() < texture.mWidth || made.getImage().getHeight() < texture.mHeight);
        count(slot);

        // **A file stood smaller than the device's side allows is short of the room**, whether the room chose the
        // level or the device came down from it, and is handed over whole again once the room is back; one the side
        // alone holds smaller stands as it can stand.
        if (texture.mSource == TextureSource::File && !made.isEmpty())
        {
            if (const std::optional<std::uint32_t> top = texture.topLevel(mSideLimit);
                top.has_value() && made.getImage().getWidth() < std::max(1u, texture.mWidth >> *top))
            {
                answer.mShort.push_back(ShortTexture{ .mSlot = texture.mSlot, .mStoodBytes = made.getBytes() });
                answer.mWants.push_back(wantOf(texture));
            }

            // What its image gives back once it rests, the range the library placed it in.
            answer.mTextureFits.push_back(FitBytes{ .mSlot = texture.mSlot,
                .mBytes = made.getImage().getMemoryBytes(),
                .mAlignment = made.getImage().getMemoryAlignment() });
        }

        if (!why.empty())
            answer.mRefused.push_back(
                Refusal{ .mKind = Refused::Texture, .mName = std::string(texture.mName), .mWhy = why });

        if ((texture.mSource == TextureSource::GroundComposite || texture.mSource == TextureSource::GroundGloss)
            && !slot.mStandIn)
            mPendingComposites.push_back(PendingComposite{ .mSlot = texture.mSlot,
                .mMaterial = std::get<MaterialIndex>(texture.mFrom),
                .mOutput = texture.mSource == TextureSource::GroundGloss ? Shaders::GROUND_COMPOSITE_GLOSS
                                                                         : Shaders::GROUND_COMPOSITE_ALBEDO });

        owe(texture.mSlot);
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

    void TextureArray::waitAgain(DeviceAnswer& answer) const
    {
        for (const TextureIndex at : everyRow<TextureIndex>(mSlots.size()))
        {
            const Slot& slot = mSlots[at];
            if (slot.mShortOfRoom && slot.mStandIn && slot.mSource == TextureSource::File && slot.mWholeBytes > 0)
                answer.mShort.push_back(ShortTexture{ .mSlot = at, .mStoodBytes = 0, .mWholeBytes = slot.mWholeBytes });
        }
    }

    const TextureTable& TextureArray::noWants()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): a constant, built on first use.
        static const TextureTable table;
        return table;
    }

    std::uint32_t TextureArray::readersWant(const TextureTable& table, const TextureIndex slot)
    {
        if (!table.getRows().holds(slot) || !table.isLive(slot))
            return 0;
        // One that rests, by what its last readers asked: moved down to it, it rests smaller rather than going.
        const TextureRow& row = table.getRows()[slot];
        return row.mRest.mResting ? row.mLastWant : table.getWant(slot);
    }

    VkDeviceSize TextureArray::makeRoom(
        TextureArrival& arrival, const RoomAsk& ask, const TextureTable& table, DeviceAnswer& answer)
    {
        const VkDeviceSize bytes = ask.mBytes;
        const std::span<const TextureData> keep = ask.mKeep;
        mMovable.clear();
        for (const TextureIndex at : everyRow<TextureIndex>(mSlots.size()))
        {
            const Slot& slot = mSlots[at];
            if (slot.mStandIn || slot.mTexture.isEmpty() || slot.mSource != TextureSource::File)
                continue;
            if (std::ranges::any_of(keep, [&](const TextureData& texture) { return texture.mSlot == at; }))
                continue;
            mMovable.push_back(Movable{ .mAboveWant = slot.mFirst < readersWant(table, at),
                .mStanding = slot.mTexture.getBytes(),
                .mSlot = at });
        }
        // What stands finer than its readers want first, then the most bytes.
        const auto lessStanding = [](const Movable& left, const Movable& right) {
            return std::tie(left.mAboveWant, left.mStanding, left.mSlot)
                < std::tie(right.mAboveWant, right.mStanding, right.mSlot);
        };
        std::ranges::make_heap(mMovable, lessStanding);

        std::string name;
        VkDeviceSize freed = 0;
        VkDeviceSize copied = 0;
        while (freed < bytes && copied < ask.mCopyMost && !mMovable.empty())
        {
            std::ranges::pop_heap(mMovable, lessStanding);
            const TextureIndex at = mMovable.back().mSlot;
            mMovable.pop_back();
            Slot& slot = mSlots[at];
            const Image& image = slot.mTexture.getImage();
            if (image.getMipLevels() < 2 || std::max(image.getWidthAt(1), image.getHeightAt(1)) < sMoveFloorSide)
                continue;
            // **A level a run at most**: an image the run writes, a move's own target among them, is copied from in
            // the next, its writes ordered before the copy only once the run is recorded.
            if (arrival.writes(image))
                continue;

            if constexpr (sDebugNames)
                name = "texture " + std::to_string(at.at());

            // **The old one where it stays, and the new one made in the slot**, since the arrival's work names both
            // images by where they stand: a texture moved after it was named was a barrier on what moved out.
            // **The bytes it stood before, as its file's**: the owner hands it over whole again once the room holds
            // that much, and a texture that came smaller still asks the room its file needs then.
            const VkDeviceSize before = slot.mTexture.getBytes();
            forget(slot);
            mMovedFrom.push_back(std::move(slot.mTexture));
            slot.mTexture = Texture();
            if (!slot.mTexture.standMovedDown(mDevice, arrival, mMovedFrom.back(), 1, name, MemoryUse::Texture).isOk())
            {
                slot.mTexture = std::move(mMovedFrom.back());
                mMovedFrom.pop_back();
                count(slot);
                break;
            }
            slot.mReduced = true;
            ++slot.mFirst;
            count(slot);
            owe(at);
            freed += before - slot.mTexture.getBytes();
            copied += slot.mTexture.getBytes();

            // Short only where it now stands coarser than its readers want; at or finer, what stands is all it is.
            const std::uint32_t want = readersWant(table, at);
            if (slot.mFirst > want)
                answer.mShort.push_back(
                    ShortTexture{ .mSlot = at, .mStoodBytes = slot.mTexture.getBytes(), .mWholeBytes = before });
            else
                answer.mMoved.push_back(MovedTexture{ .mSlot = at,
                    .mStoodBytes = slot.mTexture.getBytes(),
                    .mFrom = static_cast<std::uint8_t>(slot.mFirst) });
            answer.mTextureFits.push_back(FitBytes{ .mSlot = at,
                .mBytes = slot.mTexture.getImage().getMemoryBytes(),
                .mAlignment = slot.mTexture.getImage().getMemoryAlignment() });

            mMovable.push_back(
                Movable{ .mAboveWant = slot.mFirst < want, .mStanding = slot.mTexture.getBytes(), .mSlot = at });
            std::ranges::push_heap(mMovable, lessStanding);
        }
        return freed;
    }

    VkDeviceSize TextureArray::moveOut(TextureArrival& arrival, const VkDeviceSize budget)
    {
        // What the last call moved out of was handed to the queue with the placement that recorded it.
        mMovedFrom.clear();

        const bool asked = mDevice.getMemory().asksForBlock(mRefusedSeen);
        if (!mEmptying.has_value())
        {
            if (!asked || mFruitlessAt == mChanges)
                return 0;
            chooseBlock();
            if (!mEmptying.has_value())
            {
                mFruitlessAt = mChanges;
                return 0;
            }
        }

        const VkDeviceMemory block = *mEmptying;
        std::string name;
        VkDeviceSize copied = 0;
        while (copied < budget && !mLeaving.empty())
        {
            const TextureIndex at = mLeaving.back();
            mLeaving.pop_back();
            Slot& slot = mSlots[at];
            // Gone, or moved since the walk: what stands in the slot now stands elsewhere.
            if (slot.mStandIn || !standsIn(slot.mTexture, block))
                continue;
            // A composite the bake has yet to write into, which an arrival since the walk stood here, ends the
            // block.
            if (awaitsBake(at) || arrival.writes(slot.mTexture.getImage()))
            {
                mLeaving.clear();
                break;
            }

            if constexpr (sDebugNames)
                name = "texture " + std::to_string(at.at());

            // The old one where it stays and the new one made in the slot, as `makeRoom` moves them.
            forget(slot);
            mMovedFrom.push_back(std::move(slot.mTexture));
            slot.mTexture = Texture();
            if (!slot.mTexture.standMovedOut(mDevice, arrival, mMovedFrom.back(), block, name).isOk())
            {
                slot.mTexture = std::move(mMovedFrom.back());
                mMovedFrom.pop_back();
                count(slot);
                // No room elsewhere is no room until a slot changes, as a walk that found no block.
                mLeaving.clear();
                mFruitlessAt = mChanges;
                break;
            }
            count(slot);
            owe(at);
            const VkDeviceSize bytes
                = slot.mTexture.getImage().getMemoryBytes() + slot.mTexture.getCompanion().getMemoryBytes();
            copied += bytes;
            mMovedOut += bytes;
        }
        if (mLeaving.empty())
            mEmptying.reset();
        return copied;
    }

    void TextureArray::chooseBlock()
    {
        assert(!mEmptying.has_value());
        assert(mLeaving.empty());

        mBlockRanges.clear();
        const auto gather = [this](const DeviceMemory& memory, const bool pinned) {
            if (memory.standsInContentBlock())
                mBlockRanges.push_back(
                    BlockRange{ .mBlock = memory.getHandle(), .mBytes = memory.getSize(), .mPinned = pinned });
        };
        for (const TextureIndex at : everyRow<TextureIndex>(mSlots.size()))
        {
            const Slot& slot = mSlots[at];
            if (slot.mStandIn || slot.mTexture.isEmpty())
                continue;
            const bool pinned = awaitsBake(at);
            gather(slot.mTexture.getImage().getMemory(), pinned);
            gather(slot.mTexture.getCompanion().getMemory(), pinned);
        }
        if (mCanvas.mAlbedo.has_value())
            gather(mCanvas.mAlbedo->getMemory(), true);
        if (mCanvas.mGloss.has_value())
            gather(mCanvas.mGloss->getMemory(), true);
        if (mCanvas.mBlocks.has_value())
            gather(mCanvas.mBlocks->getMemory(), true);

        // One entry a block, its ranges summed and pinned where any is.
        std::ranges::sort(mBlockRanges, std::less{}, &BlockRange::mBlock);
        std::size_t blocks = 0;
        for (const BlockRange& range : mBlockRanges)
        {
            if (blocks > 0 && mBlockRanges[blocks - 1].mBlock == range.mBlock)
            {
                mBlockRanges[blocks - 1].mBytes += range.mBytes;
                mBlockRanges[blocks - 1].mPinned = mBlockRanges[blocks - 1].mPinned || range.mPinned;
                continue;
            }
            mBlockRanges[blocks++] = range;
        }
        mBlockRanges.resize(blocks);

        // The emptiest that may move, where the others have room for what it holds, as far as the array knows them.
        const auto emptiest
            = std::ranges::min_element(mBlockRanges, [](const BlockRange& left, const BlockRange& right) {
                  return std::tie(left.mPinned, left.mBytes) < std::tie(right.mPinned, right.mBytes);
              });
        if (mBlockRanges.size() < 2 || emptiest->mPinned)
            return;
        VkDeviceSize roomElsewhere = 0;
        for (const BlockRange& other : mBlockRanges)
            if (other.mBlock != emptiest->mBlock)
                roomElsewhere += sContentBlockBytes - std::min(other.mBytes, sContentBlockBytes);
        if (roomElsewhere < emptiest->mBytes)
            return;

        const VkDeviceMemory block = emptiest->mBlock;
        for (const TextureIndex at : everyRow<TextureIndex>(mSlots.size()))
        {
            const Slot& slot = mSlots[at];
            if (!slot.mStandIn && standsIn(slot.mTexture, block))
                mLeaving.push_back(at);
        }
        mEmptying = block;
    }

    bool TextureArray::awaitsBake(const TextureIndex slot) const
    {
        return std::ranges::any_of(
            mPendingComposites, [slot](const PendingComposite& pending) { return pending.mSlot == slot; });
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

            queueWrite(set, Shaders::TEXTURE_BIND_IMAGES, at.at(), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, held.describe(),
                mImageScratch, mWriteScratch);
            // A companion only where a read reaches one, a normal map's: the binding is partially bound.
            if (held.hasCompanion())
                queueWrite(set, Shaders::TEXTURE_BIND_COMPANIONS, at.at(), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    held.describeCompanion(), mImageScratch, mWriteScratch);
        }

        updateSets(mDevice, mWriteScratch);

        owed.clear();
    }

    Result<void, MemoryRefusal> TextureArray::makeCanvas(const TextureSource source)
    {
        if (!mCanvas.mBlocks.has_value())
        {
            Result<Buffer, MemoryRefusal> blocks = GroundCompositePass::makeBlocks(mDevice, "composite blocks");
            if (!blocks.isOk())
                return Err{ blocks.error() };
            mCanvas.mBlocks.emplace(std::move(blocks.value()));
            mHeld.mCanvasBytes += priceBlocks().standing();
        }

        const bool gloss = source == TextureSource::GroundGloss;
        std::optional<Image>& canvas = gloss ? mCanvas.mGloss : mCanvas.mAlbedo;
        if (!canvas.has_value())
        {
            Result<Image, MemoryRefusal> made
                = GroundCompositePass::makeCanvas(mDevice, gloss, gloss ? "gloss canvas" : "composite canvas");
            if (!made.isOk())
                return Err{ made.error() };
            canvas.emplace(std::move(made.value()));
            mHeld.mCanvasBytes += priceCanvas().standing();
        }
        return {};
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
                const Texture& held = mSlots[pending.mSlot].mTexture;
                if (held.isEmpty())
                    continue;

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
        return baked;
    }

    void TextureArray::drop(std::span<const TextureIndex> slots)
    {
        for (const TextureIndex slot : slots)
        {
            // A slot this array never held: a scene can add a texture and sweep it in the same
            // window, before anything was handed over to upload it.
            if (!mSlots.holds(slot))
                continue;

            // Emptied rather than erased, so the slot stays where it is and the images go under the
            // frame that may still name them.
            forget(mSlots[slot]);
            mSlots[slot].mTexture = Texture();
            mSlots[slot].mStandIn = false;
            mSlots[slot].mReduced = false;
            mSlots[slot].mShortOfRoom = false;
        }
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

    void TextureArray::queueWrite(const VkDescriptorSet set, const std::uint32_t binding, const std::uint32_t slot,
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
            .dstArrayElement = slot,
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

    void TextureArray::forget(const Slot& slot)
    {
        if (slot.mShortOfRoom)
        {
            assert(mHeld.mShortOfRoom > 0 && "a slot short of room taken off a count it was never on");
            --mHeld.mShortOfRoom;
        }

        // Whether it is there and not its size: a slot stands a texture or it does not, and a
        // content file carrying an empty level is a texture that exists.
        if (slot.mTexture.isEmpty())
            return;

        assert(mHeld.mCount > 0 && mHeld.mBytes >= slot.mTexture.getBytes() && (!slot.mReduced || mHeld.mReduced > 0)
            && "a slot taken off the totals that were never counted");
        --mHeld.mCount;
        mHeld.mBytes -= slot.mTexture.getBytes();
        bytesBySource(mHeld, slot) -= slot.mTexture.getBytes();
        if (slot.mReduced)
            --mHeld.mReduced;
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

    void TextureArray::count(const Slot& slot)
    {
        ++mChanges;
        if (slot.mShortOfRoom)
            ++mHeld.mShortOfRoom;

        if (slot.mTexture.isEmpty())
            return;

        ++mHeld.mCount;
        mHeld.mBytes += slot.mTexture.getBytes();
        bytesBySource(mHeld, slot) += slot.mTexture.getBytes();
        if (slot.mReduced)
            ++mHeld.mReduced;
    }
}
