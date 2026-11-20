#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>

#include <components/rtx/common/bufferpool.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>

namespace Rtx::Testing
{
    /// **A chain as a test reads it**: the pooled ask the hands make (`ContentPreprocessor::chain` into a pool), its
    /// bytes copied out of the pool's buffer, or nothing where the run was not admitted.
    template <class Input>
    std::optional<MadeChain> chainOf(ContentPreprocessor& content, const Input& input)
    {
        BufferPool pool(std::size_t{ 256 } << 20);
        PooledChain pooled;
        bool made = false;
        if constexpr (std::is_same_v<Input, ConversionPass::Input>)
            made = content.convert(input, 0, pool, std::stop_token{}, pooled);
        else
            made = content.chain(input, 0, pool, std::stop_token{}, pooled);
        if (!made)
            return std::nullopt;
        MadeChain chain{ .mForm = pooled.mForm, .mBytes = {} };
        const std::span<const std::byte> bytes = std::as_const(pooled.mBytes).bytes();
        chain.mBytes.assign(bytes.begin(), bytes.end());
        return chain;
    }
}
