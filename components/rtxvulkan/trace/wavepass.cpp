#include "wavepass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <osg/Vec2f>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/wave.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The amplitudes, how fast each turns, the three packed fields the rows leave, and the
        /// twiddles.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::WAVE_ROWS_BINDINGS> sRowsBindings
            = computeBindings<Shaders::WAVE_ROWS_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        /// The fields in, the two textures out, and the twiddles.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::WAVE_COLUMNS_BINDINGS> sColumnsBindings{
            computeBinding(Shaders::WAVE_COLUMNS_BIND_FIELD, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            computeBinding(Shaders::WAVE_COLUMNS_BIND_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::WAVE_COLUMNS_BIND_CURVATURE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::WAVE_BIND_TWIDDLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// How many complex numbers the transform runs over for one tile: three packed fields, each
        /// the grid itself.
        std::size_t fieldOf(std::size_t grid)
        {
            return 3 * grid * grid;
        }

        std::uint32_t gridOf(std::size_t cascade)
        {
            return static_cast<std::uint32_t>(sWaveTiles[cascade].mGrid);
        }

        Buffer uploadTwiddles(const Device& device)
        {
            const std::array<osg::Vec2f, Shaders::WAVE_TWIDDLES> twiddles = waveTwiddles();

            Batch batch(device.getPool());
            Buffer uploaded = uploadBuffer(
                batch, std::span<const osg::Vec2f>(twiddles), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "wave twiddles");
            batch.flush();
            return uploaded;
        }

        /// What a capture calls one of a cascade's objects, or nothing where no build names any:
        /// the formatting is a trip to the heap for a name that goes nowhere.
        std::string tileName([[maybe_unused]] std::string_view what, [[maybe_unused]] std::size_t cascade)
        {
            if constexpr (sDebugNames)
                return std::format("wave {} {}", what, cascade);
            else
                return {};
        }
    }

    WavePass::WavePass(const Device& device)
        : mDevice(device)
        , mRowsPipeline(device, sRowsBindings, {}, "waverows.comp.spv", "wave rows")
        , mColumnsPipeline(device, sColumnsBindings, {}, "wavecolumns.comp.spv", "wave columns")
        , mSampler(makeContentSampler(device, "wave"))
        , mTwiddles(uploadTwiddles(device))
    {
        constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        for (std::size_t index = 0; index < Shaders::WAVE_CASCADES; ++index)
        {
            Tile& tile = mTiles[index];
            const std::uint32_t grid = gridOf(index);
            const std::uint32_t levels = sWaveTiles[index].levels();

            tile.mField = Buffer::deviceLocal(mDevice, fieldOf(sWaveTiles[index].mGrid) * 2 * sizeof(float),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, tileName("field", index));

            tile.mSurface = Image(
                mDevice, grid, grid, toVulkanFormat(WAVE_TILE_FORMAT), usage, tileName("surface", index), levels);
            tile.mCurvature = Image(
                mDevice, grid, grid, toVulkanFormat(WAVE_TILE_FORMAT), usage, tileName("curvature", index), levels);
        }

        describe(mSea);

        // Every tile in the layout the trace binds it in, from the first frame: a frame with no
        // water synthesises nothing and binds the tiles anyway, and a descriptor naming an image
        // that was never transitioned is an error whether or not a ray samples it.
        mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) { record(commands, osg::Vec2f(), nullptr); });
    }

    void WavePass::describe(const SeaState& sea)
    {
        const std::array<WaveCascade, Shaders::WAVE_CASCADES> cascades = makeWaveCascades(sea);

        mSlope = waveSlope(cascades);
        mCurvature = waveCurvature(cascades);

        Batch batch(mDevice.getPool());
        for (std::size_t index = 0; index < Shaders::WAVE_CASCADES; ++index)
        {
            mTiles[index].mAmplitudes = uploadBuffer(batch, std::span<const osg::Vec2f>(cascades[index].mAmplitudes),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, tileName("amplitudes", index));
            mTiles[index].mTurnRates = uploadBuffer(batch, std::span<const float>(cascades[index].mTurnRates),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, tileName("turn rates", index));
        }
        batch.flush();

        mSea = sea;
        mSynthesised.reset();
    }

    void WavePass::handOver(VkCommandBuffer commands) const
    {
        // The synthesis is a dispatch and the trace that samples what it left is a launch. Written
        // as well as read, because the transform runs in place and a dependency naming only the
        // read leaves the two writes unordered.
        Rtx::handOver(commands, Use::sBufferComputeWrite,
            BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT });
    }

    void WavePass::record(VkCommandBuffer commands, const osg::Vec2f& seconds, GpuTimer* timer) const
    {
        const GpuZone timed(timer, commands, FrameZone::Waves);

        mSynthesised = seconds;

        // **The cascades in step, one barrier a stage for both.** A cascade's stages wait on each
        // other and never on the other cascade's, so each stage is dispatched for every tile before
        // the queue drains: one tile at a time drained it after every dispatch of both.

        // Every level is written whole below, so none needs what the last frame left in it. The
        // last frame's trace may still be sampling it, and the head barrier `CommandPool::begin`
        // recorded is what orders this buffer after that.
        std::array<const Image*, 2 * Shaders::WAVE_CASCADES> images{};
        for (std::size_t index = 0; index < Shaders::WAVE_CASCADES; ++index)
        {
            images[2 * index] = &mTiles[index].mSurface;
            images[2 * index + 1] = &mTiles[index].mCurvature;
        }

        Barriers opened(commands);
        for (const Image* image : images)
            image->addTransition(opened, Use::sUndefined, Use::sComputeWrite);
        opened.flush();

        // **Two dispatches a cascade and one barrier between them.** The rows form the spectrum
        // where they transform it, a row a workgroup and the three fields together; the columns
        // transform it and write the texels, a column a workgroup. The fields go through memory
        // once, between the halves: a pass of its own forming them or unpacking them would be
        // another trip of all three.
        for (std::size_t index = 0; index < Shaders::WAVE_CASCADES; ++index)
        {
            const Tile& tile = mTiles[index];
            const std::uint32_t grid = gridOf(index);

            DescriptorWrites rows(mRowsPipeline);
            rows.buffer(Shaders::WAVE_ROWS_BIND_AMPLITUDES, tile.mAmplitudes.describe());
            rows.buffer(Shaders::WAVE_ROWS_BIND_TURN_RATES, tile.mTurnRates.describe());
            rows.buffer(Shaders::WAVE_ROWS_BIND_FIELD, tile.mField.describe());
            rows.buffer(Shaders::WAVE_BIND_TWIDDLES, mTwiddles.describe());

            const Shaders::WaveRowsConstants shaped{
                .mCount = grid,
                .mExtent = sWaveTiles[index].mExtent,
                .mTime = seconds,
            };
            dispatch(commands, mRowsPipeline, rows, shaped, Groups{ .mX = grid });
        }
        handOver(commands);

        for (std::size_t index = 0; index < Shaders::WAVE_CASCADES; ++index)
        {
            const Tile& tile = mTiles[index];
            const std::uint32_t grid = gridOf(index);

            DescriptorWrites columns(mColumnsPipeline);
            columns.buffer(Shaders::WAVE_COLUMNS_BIND_FIELD, tile.mField.describe());
            columns.image(Shaders::WAVE_COLUMNS_BIND_SURFACE, tile.mSurface.describeStorage());
            columns.image(Shaders::WAVE_COLUMNS_BIND_CURVATURE, tile.mCurvature.describeStorage());
            columns.buffer(Shaders::WAVE_BIND_TWIDDLES, mTwiddles.describe());

            const Shaders::WaveColumnsConstants unpacked{ .mCount = grid };
            dispatch(commands, mColumnsPipeline, columns, unpacked, Groups{ .mX = grid });
        }

        Image::buildMips(commands, images);
    }
}
