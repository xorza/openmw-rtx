#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>
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

    /// A Vulkan handle and how it ends, which is all that tells one kind of handle from another:
    /// `Ending::end` ends it, `Ending::parent` is what a `vkCreateX` takes it from. The one
    /// statement of an owning handle's moves, so the three kinds below share them.
    template <class Handle, class Ending>
    class Handled
    {
    public:
        Handled() = default;

        Handled(Ending ending, Handle handle)
            : mEnding(ending)
            , mHandle(handle)
        {
        }

        /// For an ending that needs nothing but the handle: `Root`.
        explicit Handled(Handle handle) requires std::is_empty_v<Ending> : mHandle(handle) {}

        ~Handled() { reset(); }

        Handled(const Handled&) = delete;
        Handled& operator=(const Handled&) = delete;

        Handled(Handled&& other) noexcept
            : mEnding(other.mEnding)
            , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
        {
        }

        Handled& operator=(Handled&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                mEnding = other.mEnding;
                mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            }
            return *this;
        }

        Handle get() const { return mHandle; }

        /// One made by `create(parent, &info, allocator, out)`, which is the shape every
        /// `vkCreateX` this backend calls has but the pipelines' — checked, and named by `call`
        /// in the message a failure carries. The string stays: nothing in C++ names a function
        /// pointer's function.
        ///
        /// **Made into a local and adopted only once it succeeded**: a failed create leaves its
        /// output undefined, and an owner it wrote into would end whatever the driver left there.
        template <class Create, class Info>
        static Handled make(const Ending& ending, Create create, const Info& info, const char* call)
        {
            Handle made = VK_NULL_HANDLE;
            checkVk(create(ending.parent(), &info, nullptr, &made), call);
            return Handled(ending, made);
        }

        /// Ends the handle, where there is one.
        void reset()
        {
            if (mHandle != VK_NULL_HANDLE)
                mEnding.end(mHandle);
            mHandle = VK_NULL_HANDLE;
        }

    private:
        [[no_unique_address]] Ending mEnding{};
        Handle mHandle = VK_NULL_HANDLE;
    };

    /// Ends a handle of `device`'s through its graveyard.
    template <class Handle, auto& Destroy>
    struct Buried
    {
        /// Implicit, so an owner is made from the device it belongs to.
        Buried(const Device& device)
            : mDevice(&device)
        {
        }

        Buried() = default;

        VkDevice parent() const { return vulkanHandleOf(*mDevice); }

        void end(Handle handle) const { buryHandle(*mDevice, &endNow, std::bit_cast<std::uint64_t>(handle)); }

        static void endNow(const Device& device, const std::uint64_t handle)
        {
            Destroy(vulkanHandleOf(device), std::bit_cast<Handle>(handle), nullptr);
        }

        const Device* mDevice = nullptr;
    };

    /// Ends a handle with `destroy(parent, handle, allocator)` the moment it is let go of.
    template <class Parent, class Handle, class Destroy>
    struct EndedWith
    {
        EndedWith() = default;

        /// Implicit, so an owner is made from the parent it belongs to.
        EndedWith(Parent parent, Destroy destroy = {})
            : mParent(parent)
            , mDestroy(destroy)
        {
        }

        Parent parent() const { return mParent; }

        void end(Handle handle) const { mDestroy(mParent, handle, nullptr); }

        Parent mParent = VK_NULL_HANDLE;
        Destroy mDestroy{};
    };

    /// `Destroy`, a volk entry point, read at the call: a reference, because volk fills its pointers
    /// when the loader is loaded.
    template <auto& Destroy>
    struct Entry
    {
        template <class... Arguments>
        void operator()(Arguments... arguments) const
        {
            Destroy(arguments...);
        }
    };

    /// Ends a handle with no parent with `Destroy(handle, allocator)`.
    template <class Handle, auto& Destroy>
    struct EndedAlone
    {
        void end(Handle handle) const { Destroy(handle, nullptr); }
    };

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
    using Owned = Handled<Handle, Buried<Handle, Destroy>>;

    /// A handle ended the moment it is let go of, for the few the graveyard cannot outlive: the
    /// timeline's own semaphore and the pipeline cache, which the device takes apart after its
    /// graveyard, and the swapchain, whose surface goes before the device does and which is only
    /// ever remade with the device idle.
    template <class Handle, auto& Destroy>
    using Immediate = Handled<Handle, EndedWith<VkDevice, Handle, Entry<Destroy>>>;

    /// A handle with no parent, ended by `Destroy(handle, allocator)` the moment it is let go of:
    /// the instance and the logical device, which outlive everything made on them and are ended
    /// last. A member declared before everything made on its handle, so that whatever ends the
    /// owner — its destructor or a constructor that throws half way — ends those first and this
    /// last, in the one order.
    template <class Handle, auto& Destroy>
    using Root = Handled<Handle, EndedAlone<Handle, Destroy>>;
}
