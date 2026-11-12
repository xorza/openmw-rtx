#include "palette.hpp"

#include <QColor>

#include "colour.hpp"

namespace Graphite
{
    namespace
    {
        /// WCAG's floor for text against what it sits on.
        constexpr double sTextFloor = 4.5;

        /// palantir's `ACCENT_HOVER` and `ACCENT_PRESS`.
        constexpr double sAccentHover = 0.12;
        constexpr double sAccentPress = 0.24;

        QRgb withAlpha(QRgb colour, int alpha)
        {
            return qRgba(qRed(colour), qGreen(colour), qBlue(colour), alpha);
        }
    }

    QRgb selectionWash(const Palette& palette)
    {
        for (int alpha = sSelectionMost; alpha > 0; --alpha)
        {
            const QRgb wash = withAlpha(palette.mAccent, alpha);
            if (contrast(palette.mText, over(wash, palette.mElementMid)) >= sTextFloor)
                return wash;
        }
        return withAlpha(palette.mAccent, 0);
    }

    QRgb accentHovered(const Palette& palette)
    {
        return mixLinear(palette.mAccent, qRgb(255, 255, 255), sAccentHover);
    }

    QRgb accentPressed(const Palette& palette)
    {
        return mixLinear(palette.mAccent, qRgb(255, 255, 255), sAccentPress);
    }

    QRgb errorInk(const Palette& palette)
    {
        return liftedToClear(palette.mError, palette.mElementMid, sTextFloor);
    }

    QPalette qtPaletteOf(const Palette& palette)
    {
        QPalette qt;
        const auto both = [&](QPalette::ColorRole role, QRgb colour) {
            qt.setColor(QPalette::Active, role, QColor::fromRgba(colour));
            qt.setColor(QPalette::Inactive, role, QColor::fromRgba(colour));
            qt.setColor(QPalette::Disabled, role, QColor::fromRgba(colour));
        };

        // The grounds. A view or a text area is the window's own ground, as a palantir panel is; a
        // field's fill is the style's to draw, on `mElementMid`.
        both(QPalette::Window, palette.mWindow);
        both(QPalette::Base, palette.mWindow);
        both(QPalette::AlternateBase, palette.mSurround);
        both(QPalette::ToolTipBase, palette.mElement);
        both(QPalette::Button, palette.mElementMid);

        // The ladder Fusion's bevels and outlines read, lightest first.
        both(QPalette::Light, palette.mElementStrong);
        both(QPalette::Midlight, palette.mElementMid);
        both(QPalette::Mid, palette.mElement);
        both(QPalette::Dark, palette.mSurround);
        both(QPalette::Shadow, qRgb(qRed(palette.mShadow), qGreen(palette.mShadow), qBlue(palette.mShadow)));

        // The inks.
        both(QPalette::WindowText, palette.mText);
        both(QPalette::Text, palette.mText);
        both(QPalette::ButtonText, palette.mText);
        both(QPalette::ToolTipText, palette.mText);
        both(QPalette::PlaceholderText, palette.mTextMuted);
        both(QPalette::BrightText, errorInk(palette));
        both(QPalette::HighlightedText, palette.mText);

        both(QPalette::Highlight, selectionWash(palette));
        both(QPalette::Accent, palette.mAccent);
        both(QPalette::Link, palette.mAccent);
        both(QPalette::LinkVisited, palette.mAccent);

        // A disabled control: its fill one rung down, its ink the disabled one.
        qt.setColor(QPalette::Disabled, QPalette::Button, QColor::fromRgba(palette.mElement));
        for (const QPalette::ColorRole ink : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                 QPalette::HighlightedText, QPalette::PlaceholderText })
            qt.setColor(QPalette::Disabled, ink, QColor::fromRgba(palette.mTextDisabled));

        return qt;
    }
}
