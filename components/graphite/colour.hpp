#pragma once

#include <QRgb>

/// The colour arithmetic the look is derived with: WCAG's contrast, Qt's blend, and a lift at a
/// colour's own hue. Every colour is 8-bit sRGB, as `QRgb` holds it.
namespace Graphite
{
    /// WCAG's relative luminance of `colour`'s decoded channels; its alpha does not count
    /// (<https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum>).
    double luminance(QRgb colour);

    /// WCAG's contrast ratio of the two colours, from 1 for one colour to 21 for black on white.
    double contrast(QRgb first, QRgb second);

    /// `ink` at its alpha over the opaque `ground`, blended in encoded sRGB as `QPainter` blends a
    /// translucent fill: opaque, each channel rounded.
    QRgb over(QRgb ink, QRgb ground);

    /// `from` moved `amount` of the way to `to` in linear light: opaque, each channel rounded.
    QRgb mixLinear(QRgb from, QRgb to, double amount);

    /// `ink` itself where it reads `floor` on `ground`; else the least lifted colour at `ink`'s own
    /// Oklab hue that does, its chroma kept where sRGB holds it and cut to what it holds where it
    /// does not, rounded to the 8-bit grid without falling under the floor. For an ink lighter than
    /// its ground: a dark ink on a light ground is not what any look here paints.
    QRgb liftedToClear(QRgb ink, QRgb ground, double floor);
}
