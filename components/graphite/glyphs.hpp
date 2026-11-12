#pragma once

#include <QIcon>
#include <QRgb>

namespace Graphite
{
    /// The glyphs the look draws where a style is asked for a stock icon, Lucide's line icons.
    enum class Glyph
    {
        Question,
        Information,
        Warning,
        Critical,
    };

    /// `glyph` drawn in `ink` at any size and scale, in the disabled ink when disabled.
    QIcon glyphIcon(Glyph glyph, QRgb ink);
}
