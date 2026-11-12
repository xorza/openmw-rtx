#pragma once

namespace Graphite
{
    /// The text's size, palantir's title step, in logical pixels (`docs/look.md`, "The system's family at
    /// 16 px").
    inline constexpr int sBodyPixels = 16;

    /// Dresses the process's `QApplication`, which must stand, in the look whatever the system's
    /// scheme: the style over Fusion, the palette and the system's family at `sBodyPixels`, and the colour scheme asked
    /// for as dark, which a platform whose theme takes the request answers, Windows with a dark title bar.
    void apply();
}
