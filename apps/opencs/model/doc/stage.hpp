#pragma once

#include <cstdint>

namespace CSMDoc
{
    class Messages;
    class Stage
    {
    public:
        virtual ~Stage() = default;

        virtual std::int32_t setup() = 0;
        ///< \return number of steps

        virtual void perform(std::int32_t stage, Messages& messages) = 0;
        ///< Messages resulting from this stage will be appended to \a messages.
    };
}
