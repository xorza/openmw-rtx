#pragma once

namespace ESM
{
    template <class T>
    concept HasIndex = requires { T::mIndex; };

    template <class T>
    concept HasStringId = requires { T::mStringId; };
}
