#ifndef MWINPUT_GYROMANAGER
#define MWINPUT_GYROMANAGER

#include <array>

namespace MWInput
{
    class GyroManager
    {
    public:
        void update(float dt, std::array<float, 3> values);

        void setGuiCursorEnabled(bool enabled) { mGuiCursorEnabled = enabled; }

    private:
        bool mGuiCursorEnabled = true;

        // TODO: remove with the spinning camera's fix (`.notes/redesign.md` 8.1). whether the gyroscope turned the
        // camera last frame.
        bool mTurning = false;
    };
}

#endif // !MWINPUT_GYROMANAGER
