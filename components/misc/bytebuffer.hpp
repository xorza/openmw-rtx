#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Misc
{
    /// An allocator whose construction of a value with no arguments leaves it as the memory holds it:
    /// a `resize` grows a buffer and writes nothing, for one the next write fills whole. What
    /// `std::make_unique_for_overwrite` does for an array (P1020R1), which `std::vector` has no form of;
    /// Casey Carter's `default_init_allocator` is the idiom.
    template <class T>
    class DefaultInitAllocator : public std::allocator<T>
    {
    public:
        template <class U>
        struct rebind
        {
            using other = DefaultInitAllocator<U>;
        };

        DefaultInitAllocator() = default;

        template <class U>
        constexpr DefaultInitAllocator(const DefaultInitAllocator<U>& other) noexcept
            : std::allocator<T>(other)
        {
        }

        template <class U>
        void construct(U* at) noexcept(std::is_nothrow_default_constructible_v<U>)
        {
            ::new (static_cast<void*>(at)) U;
        }

        template <class U, class... Args>
        void construct(U* at, Args&&... args)
        {
            std::construct_at(at, std::forward<Args>(args)...);
        }
    };

    /// Bytes a read, a decoder or an encoder fills whole once grown: the fork's one buffer of bytes
    /// read from a file or kept as a made texture's levels, which no growth writes nought into first.
    using ByteBuffer = std::vector<std::byte, DefaultInitAllocator<std::byte>>;
}
