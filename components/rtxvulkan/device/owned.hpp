#pragma once

#include <bit>
#include <cstdint>
#include <utility>

#include <vulkan/vulkan_core.h>

#include "result.hpp"

namespace Rtx
{
    class Device;

    /// Ends one handle of `device`'s, given as its 64 bits: what the graveyard calls once no
    /// submit can read the handle any more.
    using EndHandle = void (*)(const Device& device, std::uint64_t handle);

    /// The `VkDevice` behind `device`, for this header, which the device's own includes.
    VkDevice vulkanHandleOf(const Device& device);

    /// Hands `handle` to `device`'s graveyard, which ends it with `end` once every submit made so
    /// far, and the next, has run. `Graveyard::bury`, reached from a header that cannot name it.
    void buryHandle(const Device& device, EndHandle end, std::uint64_t handle);

    /// A Vulkan handle and the device it belongs to, ended through the device's graveyard: the one
    /// place `vkDestroyX(device, handle, allocator)` is spelled, so the classes holding one default
    /// their moves, and so no owner has to know whether a submit still reads what it lets go of.
    /// `RtxSourceTreeTest` is what makes the next class adopt it, and names the calls this shape
    /// cannot take.
    ///
    /// **Deferred and never immediate**, because a handle a command buffer can name may be let go
    /// of while the queue still runs that command buffer: a resize replacing its targets, a scene
    /// replaced, a pipeline a failed compile leaves half made. What the device itself must outlive
    /// is `Immediate`.
    ///
    /// @tparam Destroy the function that ends it: a reference, because volk's entry points are
    ///         pointers it fills when the loader is loaded, and are read at the call.
    template <class Handle, auto& Destroy>
    class Owned
    {
    public:
        Owned() = default;

        Owned(const Device& device, Handle handle)
            : mDevice(&device)
            , mHandle(handle)
        {
        }

        ~Owned() { reset(); }

        Owned(const Owned&) = delete;
        Owned& operator=(const Owned&) = delete;

        Owned(Owned&& other) noexcept
            : mDevice(other.mDevice)
            , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
        {
        }

        Owned& operator=(Owned&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                mDevice = other.mDevice;
                mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            }
            return *this;
        }

        Handle get() const { return mHandle; }

        /// Where to put one, for a call that fills a handle in rather than returning it.
        Handle* put(const Device& device)
        {
            reset();
            mDevice = &device;
            return &mHandle;
        }

        /// One made by `create(device, &info, allocator, out)`, which is the shape every
        /// `vkCreateX` this backend calls has but the pipelines' — checked, and named by `call`
        /// in the message a failure carries. The string stays: nothing in C++ names a function
        /// pointer's function.
        template <class Create, class Info>
        static Owned make(const Device& device, Create create, const Info& info, const char* call)
        {
            Owned made;
            checkVk(create(vulkanHandleOf(device), &info, nullptr, made.put(device)), call);
            return made;
        }

        /// Buries the handle, where there is one.
        void reset()
        {
            if (mHandle != VK_NULL_HANDLE)
                buryHandle(*mDevice, &end, std::bit_cast<std::uint64_t>(mHandle));
            mHandle = VK_NULL_HANDLE;
        }

        /// Gives the handle up unburied, for an owner that buries it beside the memory bound to it
        /// — `Graveyard::bury`, which ends it with `end`.
        Handle release() { return std::exchange(mHandle, VK_NULL_HANDLE); }

        static void end(const Device& device, const std::uint64_t handle)
        {
            Destroy(vulkanHandleOf(device), std::bit_cast<Handle>(handle), nullptr);
        }

    private:
        const Device* mDevice = nullptr;
        Handle mHandle = VK_NULL_HANDLE;
    };

    /// A handle ended the moment it is let go of, for the few the graveyard cannot outlive: the
    /// timeline's own semaphore and the pipeline cache, which the device takes apart after its
    /// graveyard, and the swapchain, whose surface goes before the device does and which is only
    /// ever remade with the device idle.
    template <class Handle, auto& Destroy>
    class Immediate
    {
    public:
        Immediate() = default;

        Immediate(VkDevice device, Handle handle)
            : mDevice(device)
            , mHandle(handle)
        {
        }

        ~Immediate() { reset(); }

        Immediate(const Immediate&) = delete;
        Immediate& operator=(const Immediate&) = delete;

        Immediate(Immediate&& other) noexcept
            : mDevice(other.mDevice)
            , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
        {
        }

        Immediate& operator=(Immediate&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                mDevice = other.mDevice;
                mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            }
            return *this;
        }

        Handle get() const { return mHandle; }

        /// Where to put one, for a call that fills a handle in rather than returning it.
        Handle* put(VkDevice device)
        {
            reset();
            mDevice = device;
            return &mHandle;
        }

        /// `Owned::make`, for a device given as its handle.
        template <class Create, class Info>
        static Immediate make(VkDevice device, Create create, const Info& info, const char* call)
        {
            Immediate made;
            checkVk(create(device, &info, nullptr, made.put(device)), call);
            return made;
        }

        void reset()
        {
            if (mHandle != VK_NULL_HANDLE)
                Destroy(mDevice, mHandle, nullptr);
            mHandle = VK_NULL_HANDLE;
        }

    private:
        VkDevice mDevice = VK_NULL_HANDLE;
        Handle mHandle = VK_NULL_HANDLE;
    };
}
