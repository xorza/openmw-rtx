#pragma once

namespace Settings
{
    template <class T>
    struct Sanitizer
    {
        virtual ~Sanitizer() = default;

        virtual T apply(const T& value) const = 0;
    };
}
