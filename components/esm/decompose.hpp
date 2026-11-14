#pragma once

namespace ESM
{
    template <class T>
    void decompose(T&& value, const auto& apply) = delete;

    std::size_t getCompositeSize(const auto& value)
    {
        std::size_t result = 0;
        decompose(value, [&](const auto&... args) { result = (0 + ... + sizeof(args)); });
        return result;
    }
}
