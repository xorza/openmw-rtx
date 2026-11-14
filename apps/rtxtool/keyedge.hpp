#pragma once

namespace RtxTool
{
    /// One key read once a frame off the state SDL keeps: whether it went down since the last read, so a key held
    /// across frames is one press.
    class KeyEdge
    {
    public:
        /// Whether the key, `down` now, was up at the last read.
        bool wentDown(const bool down)
        {
            const bool pressed = down && !mHeld;
            mHeld = down;
            return pressed;
        }

    private:
        bool mHeld = false;
    };
}
