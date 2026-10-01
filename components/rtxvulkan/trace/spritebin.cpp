#include "spritebin.hpp"

#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/spritebin.h>
#include <components/rtx/shaders/spriteshade.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "spritepasses.hpp"

namespace Rtx
{
    SpriteBin::SpriteBin(const Device& device, const SpriteShadePass& shading, const SpriteBinPass& pass)
        : mDevice(device)
        , mShading(shading)
        , mPass(pass)
        , mSprites(device, BufferKind::DeviceLocal, sTableFilledUsage, "binned sprites")
        , mEmitterFrames(device, BufferKind::DeviceLocal, sTableUsage, "emitter frames")
        , mOrder(device, BufferKind::DeviceLocal, sTableUsage, "sprite order")
        , mRects(device, BufferKind::DeviceLocal, sTableUsage, "sprite rects")
        , mTileList(device, BufferKind::DeviceLocal, sTableFilledUsage, "sprite tile list")
        , mPresence(device, BufferKind::DeviceLocal, sTableFilledUsage, "sprite presence")
        , mReport(Buffer::readBack(device, sizeof(std::uint32_t), sTableUsage, "sprite report"))
    {
        // Every table exists from here, whether or not anything is binned: a frame carries the
        // address of the sprites and the list, and a scene with no sprites bins none.
        for (GrowableBuffer* table : { &mSprites, &mEmitterFrames, &mOrder, &mRects, &mTileList, &mPresence })
            table->growTo(0);
        mReport.writable<std::uint32_t>(0, 1).front() = 0;
    }

    void SpriteBin::take(const SpriteSource& source, const Shaders::Camera& camera, const VkCommandBuffer commands)
    {
        const Timeline& timeline = mDevice.getTimeline();
        const std::uint32_t count = source.mSpriteCount;
        const VkDeviceSize bytes = VkDeviceSize{ count } * sizeof(Shaders::GpuSprite);

        // At twice the high-water mark past it, as every table a frame writes: a storm thickens by a
        // few sprites a frame, and a table sized to each count would be made again on every one.
        mSprites.outgrow(bytes);
        mOrder.outgrow(VkDeviceSize{ count } * Shaders::SPRITE_SHADE_LIGHTS * sizeof(std::uint64_t));
        mRects.outgrow(VkDeviceSize{ count } * sizeof(std::uint64_t));
        mEmitterFrames.outgrow(VkDeviceSize{ source.mEmitterCount } * sizeof(Shaders::GpuEmitterFrame));
        mPresence.outgrow(
            VkDeviceSize{ Shaders::spriteTilesIn(camera.mWidth, camera.mHeight) } * sizeof(std::uint32_t));

        // Sized from what the last bin here said it needed, with room over it, because the need is
        // only known once the tiles are counted and that happens on the device. Read where the
        // host has waited past the submit the report rode — the frame ring's wait, a frame or two
        // on, and never a question to the device, so which frame first reads a report is a
        // function of the frames and not of the clock; a bin whose report is not yet waited for
        // keeps the capacity it has, which `SpriteListSize` never shrinks anyway. Here with the
        // rest, because the frame block carries this table's address too.
        const std::uint32_t reported
            = timeline.hasFinished(mReport.getNamedUntil()) ? *static_cast<const std::uint32_t*>(mReport.map()) : 0;
        mListSize.sizeFor(Shaders::spriteTilesIn(camera.mWidth, camera.mHeight), count, reported);

        // Past the need as the tables above are; the pass is told the capacity the rule gave, which
        // the buffer holds.
        mTileList.outgrow(mListSize.getBytes());

        // The placement's table, whole, because the shade writes over what it reads: the copy is
        // what lets a second trace against the same placement — a picture, or the frame after a
        // picture — start from sprites nothing has shaded. On the queue and not from the host,
        // so nothing a submit in flight reads is written under it. Whoever read this bin's tables
        // last is behind on the queue, and the barrier `CommandPool::begin` records at the head
        // of these commands is what orders the copy after it. Handed to the launch and the
        // dispatch both, because the shelter launch writes it before the shade does.
        if (bytes > 0)
            source.mSprites->copyTo(commands, mSprites.get(), bytes);
        mSprites.get().transition(commands, Use::sBufferCopyWrite, Use::sBufferShaderReadWrite);
    }

    void SpriteBin::record(VkCommandBuffer commands, const Binning& what)
    {
        const SpriteSource& source = what.mSource;
        const osg::Vec3f& origin = what.mOrigin;
        const Shaders::Camera& camera = what.mCamera;
        const osg::Vec3f& toSun = what.mToSun;
        GpuTimer* const timer = what.mTimer;

        const std::uint32_t count = source.mSpriteCount;
        assert(mSprites.get().getSize() >= VkDeviceSize{ source.mSpriteCount } * sizeof(Shaders::GpuSprite)
            && "a bin recorded over sprites it never took");

        mShading.record(commands,
            Shaders::SpriteShadeConstants{
                .mSprites = mSprites.get().addressFor(),
                .mEmitters = source.mEmitters,
                .mOrder = mOrder.get().addressFor(),
                .mToSun = toSun,
                .mEmitterCount = source.mEmitterCount,
                .mCount = count,
            },
            timer);

        mPass.record(commands,
            Shaders::SpriteBinConstants{
                .mSprites = mSprites.get().addressFor(),
                .mEmitters = source.mEmitters,
                .mRects = mRects.get().addressFor(),
                .mList = mTileList.get().addressFor(),
                .mReport = mReport.addressFor(),
                .mPresences = source.mPresences,
                .mPresence = mPresence.get().addressFor(),
                .mOrigin = origin,
                .mCamera = camera,
                .mCount = count,
                .mCapacity = mListSize.getCapacity(),
                .mPresenceCount = source.mPresenceCount,
            },
            mTileList.get(), mPresence.get(), timer);

        // What the next bin here sizes its list from. A fence's access scope is the device's, so
        // without this the figure is whatever the caches held.
        mReport.orderForHostRead(commands);
    }

    VkDeviceSize SpriteBin::getBytes() const
    {
        return mSprites.get().getSize() + mEmitterFrames.get().getSize() + mOrder.get().getSize()
            + mRects.get().getSize() + mTileList.get().getSize() + mPresence.get().getSize() + mReport.getSize();
    }
}
