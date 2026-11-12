#pragma once

#include <QProxyStyle>

namespace Graphite
{
    /// palantir's widget recipes, drawn over Fusion (`docs/rtx/look.md`, "Fusion, the palette and a proxy
    /// style; no style sheet"): flat fills on the palette's ladder, hairline edges, small radii, the
    /// accent for a focused edge, a check and the default button. What it does not draw, Fusion does, in
    /// the palette's colours.
    class Style final : public QProxyStyle
    {
    public:
        Style();

        void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
            const QWidget* widget) const override;
        void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
            const QWidget* widget) const override;
        void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
            const QWidget* widget) const override;
        QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl,
            const QWidget* widget) const override;
        int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override;
        QSize sizeFromContents(
            ContentsType type, const QStyleOption* option, const QSize& contents, const QWidget* widget) const override;
        int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
            QStyleHintReturn* returnData) const override;

        using QProxyStyle::polish;
        void polish(QWidget* widget) override;
    };
}
