#pragma once

#include <array>

namespace MWInput
{
    class GyroManager
    {
    public:
        void update(float dt, std::array<float, 3> values) const;

        void setGuiCursorEnabled(bool enabled) { mGuiCursorEnabled = enabled; }

    private:
        bool mGuiCursorEnabled = true;
    };
}
