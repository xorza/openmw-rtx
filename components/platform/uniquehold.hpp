#pragma once

#include <utility>

namespace Platform
{
    /// **One system handle, held by its owner alone**: move-only, closed as it goes, and a move
    /// assignment closes what it held before it takes the other's. `Traits` names the handle
    /// (`Handle`), the value that holds nothing (`sNone`) and how a handle is closed (`close`).
    template <class Traits>
    class UniqueHold
    {
    public:
        using Handle = typename Traits::Handle;

        UniqueHold() = default;

        explicit UniqueHold(Handle handle) noexcept
            : mHandle(handle)
        {
        }

        UniqueHold(UniqueHold&& other) noexcept
            : mHandle(std::exchange(other.mHandle, Traits::sNone))
        {
        }

        UniqueHold& operator=(UniqueHold&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                mHandle = std::exchange(other.mHandle, Traits::sNone);
            }
            return *this;
        }

        UniqueHold(const UniqueHold&) = delete;
        UniqueHold& operator=(const UniqueHold&) = delete;

        ~UniqueHold() { reset(); }

        Handle get() const noexcept { return mHandle; }
        bool isOpen() const noexcept { return mHandle != Traits::sNone; }

        /// Closes the handle, where one is held, and holds nothing.
        void reset() noexcept
        {
            if (isOpen())
                Traits::close(std::exchange(mHandle, Traits::sNone));
        }

    private:
        Handle mHandle = Traits::sNone;
    };
}
