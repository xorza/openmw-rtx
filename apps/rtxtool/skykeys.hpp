#pragma once

namespace RtxTool
{
    /// What `[` and `]` do in a window: step the sky one weather back or on, among the weathers the
    /// region rolls (`CameraDriver::turnSkyBy`).
    ///
    /// **SDL's own key state, as Home is read, and not a script**, because what the keys turn is the
    /// harness's own crossing (`SkyCrossing`), which no script can reach. The brackets are the
    /// game's weapon cycle, which returns before it does anything to a body carrying no weapon, as
    /// the harness's carries none.
    class SkyKeys
    {
    public:
        /// Before a frame is stepped: how many weathers on the keys that went down since the last
        /// call ask for, one back for `[` and one on for `]`, and nought for both at once.
        int listen();

    private:
        bool mBackHeld = false;
        bool mOnHeld = false;
    };
}
