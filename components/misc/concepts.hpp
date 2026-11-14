#pragma once

#include <concepts>
#include <type_traits>

namespace Misc
{
    template <class T, class U>
    concept SameAsWithoutCvref = std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<U>>;
}
