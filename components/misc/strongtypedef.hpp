#pragma once

#include <cstdint>
#include <utility>

namespace Misc
{
    template <class T, class>
    struct StrongTypedef
    {
        T mValue;

        StrongTypedef() = default;

        explicit StrongTypedef(const T& value)
            : mValue(value)
        {
        }

        explicit StrongTypedef(T&& value)
            : mValue(std::move(value))
        {
        }

        operator const T&() const { return mValue; }

        operator T&() { return mValue; }

        StrongTypedef& operator++()
        {
            ++mValue;
            return *this;
        }

        StrongTypedef operator++(std::int32_t)
        {
            StrongTypedef copy(*this);
            operator++();
            return copy;
        }
    };
}
