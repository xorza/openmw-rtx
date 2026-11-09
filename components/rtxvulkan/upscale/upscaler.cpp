#include "upscaler.hpp"

#include <cassert>
#include <string_view>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/platform/dlss.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/dlss.hpp>

namespace Rtx
{
    Upscaler::Upscaler(const Device& device, const UpscalerKind requested)
        : mDevice(device)
        , mKind(UpscalerKind::Fsr)
    {
        if (requested == UpscalerKind::Auto)
        {
            // **The library's presence is part of what `auto` means**, and the query NGX answers
            // before a device exists can be inconclusive without it. Asking the same discovery the
            // launcher and the settings window ask keeps the menu, the picture and the log agreeing;
            // an explicit `dlss` still tries and fails by name.
            if (Dlss::sCompiled && device.supportsDlss() && Platform::Dlss::installed())
            {
                try
                {
                    mDlss = std::make_unique<DlssUpscaler>(device);
                    mKind = UpscalerKind::Dlss;
                }
                catch (const Unsupported& refused)
                {
                    Log(Debug::Warning) << "NVIDIA DLSS could not start, so FSR is used: " << refused.what();
                }
            }

            if (mKind != UpscalerKind::Dlss)
                mFsr = std::make_unique<FsrUpscaler>(device);
        }
        else if (requested == UpscalerKind::Dlss)
        {
            mDlss = std::make_unique<DlssUpscaler>(device);
            mKind = UpscalerKind::Dlss;
        }
        else
        {
            mFsr = std::make_unique<FsrUpscaler>(device);
        }
    }

    Upscaler::~Upscaler() = default;

    void Upscaler::ensure(const UpscalerKind kind)
    {
        assert(kind != UpscalerKind::Auto && "an upscaler kind that was never resolved");
        if (kind == mKind && (mFsr != nullptr || mDlss != nullptr))
            return;

        mFsr.reset();
        mDlss.reset();
        mKind = kind;
        if (mKind == UpscalerKind::Dlss)
            mDlss = std::make_unique<DlssUpscaler>(mDevice);
        else
            mFsr = std::make_unique<FsrUpscaler>(mDevice);
    }

    void Upscaler::resize(
        const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        if (kind != mKind || (kind == UpscalerKind::Fsr && mFsr == nullptr)
            || (kind == UpscalerKind::Dlss && mDlss == nullptr))
            ensure(kind);

        if (mKind == UpscalerKind::Dlss)
            mDlss->resize(render, output, shared);
        else
            mFsr->resize(render, output, shared);
    }

    void Upscaler::declareShared(
        const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        switch (kind)
        {
            case UpscalerKind::Dlss:
                DlssUpscaler::declareShared(render, output, into);
                break;
            case UpscalerKind::Fsr:
            case UpscalerKind::Auto:
                FsrUpscaler::declareShared(render, output, into);
                break;
        }
    }

    std::size_t Upscaler::getSharedCount(const UpscalerKind kind)
    {
        return kind == UpscalerKind::Dlss ? DlssUpscaler::getSharedCount() : FsrUpscaler::getSharedCount();
    }

    VkDeviceSize Upscaler::bytesAt(
        const Device& device, const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output)
    {
        return kind == UpscalerKind::Dlss ? DlssUpscaler::bytesAt(device, render, output)
                                          : FsrUpscaler::bytesAt(device, render, output);
    }

    void Upscaler::release()
    {
        if (mDlss != nullptr)
            mDlss->release();
        if (mFsr != nullptr)
            mFsr->release();
    }

    bool Upscaler::isBuilt(const UpscalerKind kind) const
    {
        if (kind != mKind)
            return false;
        return mKind == UpscalerKind::Dlss ? mDlss->isBuilt() : mFsr->isBuilt();
    }

    bool Upscaler::isAt(const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output) const
    {
        if (kind != mKind)
            return false;
        return mKind == UpscalerKind::Dlss ? mDlss->isAt(render, output) : mFsr->isAt(render, output);
    }

    void Upscaler::reset()
    {
        if (mKind == UpscalerKind::Dlss)
            mDlss->reset();
        else
            mFsr->reset();
    }

    bool Upscaler::isFresh() const
    {
        return mKind == UpscalerKind::Dlss ? mDlss->isFresh() : mFsr->isFresh();
    }

    HandedImage Upscaler::record(
        const UpscalerKind kind, const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        assert(kind == mKind && "a frame asked one upscaler and the frame's is another");
        return mKind == UpscalerKind::Dlss ? mDlss->record(commands, inputs, timer)
                                           : mFsr->record(commands, inputs, timer);
    }

    std::string_view Upscaler::describe() const
    {
        return mKind == UpscalerKind::Dlss ? DlssUpscaler::describe() : FsrUpscaler::describe();
    }
}
