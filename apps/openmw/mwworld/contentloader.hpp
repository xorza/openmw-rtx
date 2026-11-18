#pragma once

#include <cstdint>
#include <filesystem>

namespace Loading
{
    class Listener;
}

namespace MWWorld
{

    struct ContentLoader
    {
        virtual ~ContentLoader() = default;

        virtual void load(const std::filesystem::path& filepath, std::int32_t& index, Loading::Listener* listener) = 0;
    };

} /* namespace MWWorld */
