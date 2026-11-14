#ifndef OPENMW_COMPONENTS_MISC_NOTNULLPTR_H
#define OPENMW_COMPONENTS_MISC_NOTNULLPTR_H

#include <cassert>
#include <cstddef>

namespace Misc
{
    template <class T>
    class NotNullPtr
    {
    public:
        // A pointer converts to the not-null pointer, which checks it.
        // NOLINTNEXTLINE(google-explicit-constructor)
        constexpr NotNullPtr(T* value) noexcept
            : mValue(value)
        {
            assert(mValue != nullptr);
        }

        NotNullPtr(std::nullptr_t) = delete;

        // The pointer reads as the raw pointer it holds.
        // NOLINTNEXTLINE(google-explicit-constructor)
        constexpr operator T*() const noexcept { return mValue; }

        constexpr T* operator->() const noexcept { return mValue; }

        constexpr T& operator*() const noexcept { return *mValue; }

    private:
        T* mValue;
    };
}

#endif
