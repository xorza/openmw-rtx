#pragma once

namespace ESM
{
    template <class T>
    concept HasId = requires { T::mId; };

    template <class T>
    concept HasModel = requires { T::mModel; };
}
