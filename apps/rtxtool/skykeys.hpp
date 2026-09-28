#pragma once

namespace RtxTool
{
    /// What the sky keys asked for since the last frame: how many weathers on, one back for `[` and
    /// one on for `]`, and nought for both at once; and whether Shift was down with them, which
    /// asks for the weather at once rather than for a crossing into it.
    struct SkyPress
    {
        int mSteps = 0;
        bool mAtOnce = false;
    };

    /// What `[` and `]` do in a window: step the sky one weather back or on, among the weathers the
    /// region rolls (`CameraDriver::turnSkyBy`), crossing into it, or standing under it at once
    /// with Shift.
    ///
    /// **SDL's own key state, as Home is read, and not a script**, because what the keys turn is the
    /// harness's own crossing (`SkyCrossing`), which no script can reach. The brackets are the
    /// game's weapon cycle, which returns before it does anything to a body carrying no weapon, as
    /// the harness's carries none.
    class SkyKeys
    {
    public:
        /// Before a frame is stepped: what the keys that went down since the last call ask for.
        SkyPress listen();

    private:
        bool mBackHeld = false;
        bool mOnHeld = false;
    };
}
