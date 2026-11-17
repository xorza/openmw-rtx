#pragma once

#include <cstdint>

namespace SDLUtil
{
    /// What a key did: went down, came again while held, which the system repeats until the key
    /// is let go, or was let go.
    enum class KeyChange
    {
        Pressed,
        Repeated,
        Released,
    };

    /// What whoever draws into the window hears from `InputWrapper`: the moments and the keys
    /// that are its own rather than the game's. Every answer is empty by default, for a listener
    /// with no use for one.
    class GraphicsListener
    {
    public:
        virtual ~GraphicsListener() = default;

        /// The events of one frame are about to be read.
        virtual void beginEvents() {}

        /// F1 to F12, as `index` nought to eleven — with no Alt held, which is the window
        /// manager's. The game's own bindings see the key as well.
        virtual void functionKey(std::int32_t index, KeyChange change) {}

        /// The window's drawable size changed. In pixels, and never nought by nought, which is a
        /// window that was minimised.
        virtual void windowResized(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {}
    };
}
