#pragma once

#include <QPalette>
#include <QRgb>

/// The colours of the launcher's and the wizard's look, palantir's stock dark palette
/// (`docs/rtx/look.md`, "The Qt programs wear palantir's dark palette").
namespace Graphite
{
    /// The semantic roster every colour of the look is drawn from, as palantir's `Palette` names it:
    /// three `element` rungs, each further from `mWindow` than the one before, and the inks on them.
    struct Palette
    {
        /// Primary ink.
        QRgb mText;

        /// De-emphasised ink, a field's placeholder; also the base of the border ladder.
        QRgb mTextMuted;

        QRgb mTextDisabled;

        /// The window's ground.
        QRgb mWindow;

        /// The band the window's controls sit on, a step off `mWindow`: a tab strip.
        QRgb mSurround;

        /// A raised or recessed surface: a menu, a tooltip, a disabled control's fill.
        QRgb mElement;

        /// A control at rest.
        QRgb mElementMid;

        /// A control under the pointer or pressed.
        QRgb mElementStrong;

        /// A focused control's edge.
        QRgb mBorderFocused;

        /// Checks, progress, the default button, the selection's tint.
        QRgb mAccent;

        /// Ink on an `mAccent` fill.
        QRgb mOnAccent;

        /// What a floating surface's shadow casts.
        QRgb mShadow;

        /// The hues palantir's palette has no role for, as Ayu Graphite's semantic layer holds them.
        QRgb mError;
        QRgb mWarning;
        QRgb mSuccess;
    };

    /// Morrowind's own leather: the mean of the main menu's art, `textures/menu_morrowind.dds` in
    /// `Morrowind.bsa`, the brown the game's title stands on.
    inline constexpr QRgb sMenuLeather = 0xff764316;

    /// palantir's `Palette::DARK` at `67955d19f`, with Ayu Graphite's `error`, `warning` and `success`, and
    /// the accent Morrowind's leather: `sMenuLeather` lifted at its own hue until it reads 3:1 on a field,
    /// as a focused edge must (`liftedToClear`, which a test holds it to).
    inline constexpr Palette sDark{
        .mText = 0xffebebeb,
        .mTextMuted = 0xffb2b2b2,
        .mTextDisabled = 0xff797979,
        .mWindow = 0xff141414,
        .mSurround = 0xff1f1f1f,
        .mElement = 0xff383838,
        .mElementMid = 0xff454545,
        .mElementStrong = 0xff525252,
        .mBorderFocused = 0xffbc8458,
        .mAccent = 0xffbc8458,
        .mOnAccent = 0xff141414,
        .mShadow = 0x80000000,
        .mError = 0xffff8b8c,
        .mWarning = 0xfff6e055,
        .mSuccess = 0xff8bdd79,
    };

    /// The border ladder: the muted ink at three alphas, as palantir's, since the surface greys are
    /// too close to `mElement` to read as a pixel-wide edge. The faintest, a rule or a field's edge.
    constexpr QRgb borderSoft(const Palette& palette)
    {
        return qRgba(qRed(palette.mTextMuted), qGreen(palette.mTextMuted), qBlue(palette.mTextMuted), 46);
    }

    /// A panel's edge, a menu's or a tooltip's.
    constexpr QRgb borderMid(const Palette& palette)
    {
        return qRgba(qRed(palette.mTextMuted), qGreen(palette.mTextMuted), qBlue(palette.mTextMuted), 56);
    }

    /// The loudest edge, a check box's.
    constexpr QRgb borderStrong(const Palette& palette)
    {
        return qRgba(qRed(palette.mTextMuted), qGreen(palette.mTextMuted), qBlue(palette.mTextMuted), 89);
    }

    /// The most of the accent, in 255ths, the selection's tint takes, so it stays a tint under any
    /// accent: palantir's `SELECTION_MAX`.
    inline constexpr int sSelectionMost = 61;

    /// The selection's tint: the accent at the most alpha, up to `sSelectionMost`, that keeps
    /// `mText` at 4.5:1 over a field washed with it, as palantir's `selection_wash` holds it. Blended
    /// in encoded sRGB, as `QPainter` blends it, where palantir blends in linear light as its
    /// renderer does.
    QRgb selectionWash(const Palette& palette);

    /// The default button's fill under the pointer and pressed: the accent moved 12 % and 24 % of the
    /// way to white in linear light, away from its dark ink, as palantir's `ButtonTheme::accent`.
    QRgb accentHovered(const Palette& palette);
    QRgb accentPressed(const Palette& palette);

    /// The ink an error is written in on a field: `mError` lifted at its own hue until it reads
    /// 4.5:1 on `mElementMid`, as palantir lifts its muted ink over Ayu Graphite's.
    QRgb errorInk(const Palette& palette);

    /// The palette as Qt's roles take it, in every colour group.
    QPalette qtPaletteOf(const Palette& palette);
}
