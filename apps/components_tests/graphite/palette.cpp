#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QColor>
#include <QPalette>
#include <QRgb>

#include <components/graphite/colour.hpp>
#include <components/graphite/palette.hpp>

namespace Graphite
{
    namespace
    {
        /// WCAG 1.4.3: text against what it sits on.
        constexpr double sText = 4.5;

        /// WCAG 1.4.11: a focus edge or a state mark against what it sits on.
        constexpr double sGraphic = 3.0;

        /// Two surfaces stacked, so one reads apart from the other: the step Ayu Graphite holds
        /// between its rungs.
        constexpr double sRung = 1.10;

        struct Pair
        {
            std::string mWhat;
            QRgb mInk;
            QRgb mGround;
            double mFloor;
        };

        /// Every pair the look paints from `p`, with its floor: palantir's `floors.rs`, and the error
        /// ink on a field.
        std::vector<Pair> pairsOf(const Palette& p)
        {
            const QRgb washed = over(selectionWash(p), p.mElementMid);
            return {
                { "text on window", p.mText, p.mWindow, sText },
                { "text on surround", p.mText, p.mSurround, sText },
                { "text on element", p.mText, p.mElement, sText },
                { "text on element_mid", p.mText, p.mElementMid, sText },
                { "text on element_strong", p.mText, p.mElementStrong, sText },
                { "text on the selection wash", p.mText, washed, sText },
                { "text_muted on surround", p.mTextMuted, p.mSurround, sText },
                { "text_muted on element", p.mTextMuted, p.mElement, sText },
                { "text_muted on element_mid", p.mTextMuted, p.mElementMid, sText },
                { "on_accent on accent", p.mOnAccent, p.mAccent, sText },
                { "on_accent on the hovered accent button", p.mOnAccent, accentHovered(p), sText },
                { "on_accent on the pressed accent button", p.mOnAccent, accentPressed(p), sText },
                { "border_focused on window", p.mBorderFocused, p.mWindow, sGraphic },
                { "border_focused on surround", p.mBorderFocused, p.mSurround, sGraphic },
                { "border_focused on element_mid", p.mBorderFocused, p.mElementMid, sGraphic },
                { "accent on element_mid", p.mAccent, p.mElementMid, sGraphic },
                { "window to surround", p.mWindow, p.mSurround, sRung },
                { "surround to element_mid", p.mSurround, p.mElementMid, sRung },
                { "element_mid to element_strong", p.mElementMid, p.mElementStrong, sRung },
                { "the error ink on element_mid", errorInk(p), p.mElementMid, sText },
            };
        }

        std::vector<std::string> missesOf(const Palette& p)
        {
            std::vector<std::string> misses;
            for (const Pair& pair : pairsOf(p))
                if (contrast(pair.mInk, pair.mGround) < pair.mFloor)
                    misses.push_back(pair.mWhat);
            return misses;
        }

        /// **Every pair the look paints clears its floor**, and the test reads what it claims: a
        /// palette whose muted ink is its field's fill fails on exactly the three muted pairs.
        TEST(GraphitePaletteTest, everyPairTheLookPaintsClearsItsFloor)
        {
            EXPECT_EQ(missesOf(sDark), std::vector<std::string>{});

            Palette muted = sDark;
            muted.mTextMuted = sDark.mElementMid;
            EXPECT_EQ(missesOf(muted),
                (std::vector<std::string>{
                    "text_muted on surround", "text_muted on element", "text_muted on element_mid" }));
        }

        /// **The derived colours, each worked out by hand from the table.** The ladder is `#b2b2b2` at
        /// 46, 56 and 89 of 255. The accent is the main menu's leather `#764316` lifted at its hue to
        /// `#bc8458`, 3.00:1 on `#454545`, where a step less misses it. The wash is the accent at its cap of
        /// 61: over `#454545` each channel is (c · 61 + 69 · 194) / 255, (188, 132, 88) to (97, 84, 74),
        /// where `#ebebeb` reads past 4.5:1. The accent button moves 12 % and 24 % of the way to white in
        /// linear light, `#c69a7d` and `#cfad98`. The error ink is `#ff8b8c` lifted to `#ff9494`.
        TEST(GraphitePaletteTest, theDerivedColoursAreTheTablesWorkedOut)
        {
            EXPECT_EQ(borderSoft(sDark), qRgba(0xb2, 0xb2, 0xb2, 46));
            EXPECT_EQ(borderMid(sDark), qRgba(0xb2, 0xb2, 0xb2, 56));
            EXPECT_EQ(borderStrong(sDark), qRgba(0xb2, 0xb2, 0xb2, 89));

            EXPECT_EQ(liftedToClear(sMenuLeather, sDark.mElementMid, 3.0), sDark.mAccent);
            EXPECT_EQ(sDark.mBorderFocused, sDark.mAccent);

            EXPECT_EQ(selectionWash(sDark), qRgba(0xbc, 0x84, 0x58, sSelectionMost));
            EXPECT_EQ(over(selectionWash(sDark), sDark.mElementMid), qRgb(97, 84, 74));
            EXPECT_GE(contrast(sDark.mText, over(selectionWash(sDark), sDark.mElementMid)), 4.5);
            EXPECT_EQ(accentHovered(sDark), qRgb(0xc6, 0x9a, 0x7d));
            EXPECT_EQ(accentPressed(sDark), qRgb(0xcf, 0xad, 0x98));
            EXPECT_EQ(errorInk(sDark), qRgb(0xff, 0x94, 0x94));
        }

        /// **Qt's roles carry the table**, the same in the active and the inactive group, and a
        /// disabled control's fill one rung down and its inks the disabled one.
        TEST(GraphitePaletteTest, qtsRolesCarryTheTable)
        {
            const QPalette qt = qtPaletteOf(sDark);
            const auto at
                = [&](QPalette::ColorGroup group, QPalette::ColorRole role) { return qt.color(group, role).rgba(); };
            for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive })
            {
                EXPECT_EQ(at(group, QPalette::Window), sDark.mWindow);
                EXPECT_EQ(at(group, QPalette::Base), sDark.mWindow);
                EXPECT_EQ(at(group, QPalette::AlternateBase), sDark.mSurround);
                EXPECT_EQ(at(group, QPalette::ToolTipBase), sDark.mElement);
                EXPECT_EQ(at(group, QPalette::Button), sDark.mElementMid);
                EXPECT_EQ(at(group, QPalette::Light), sDark.mElementStrong);
                EXPECT_EQ(at(group, QPalette::Midlight), sDark.mElementMid);
                EXPECT_EQ(at(group, QPalette::Mid), sDark.mElement);
                EXPECT_EQ(at(group, QPalette::Dark), sDark.mSurround);
                EXPECT_EQ(at(group, QPalette::Shadow), qRgb(0, 0, 0));
                EXPECT_EQ(at(group, QPalette::WindowText), sDark.mText);
                EXPECT_EQ(at(group, QPalette::Text), sDark.mText);
                EXPECT_EQ(at(group, QPalette::ButtonText), sDark.mText);
                EXPECT_EQ(at(group, QPalette::ToolTipText), sDark.mText);
                EXPECT_EQ(at(group, QPalette::PlaceholderText), sDark.mTextMuted);
                EXPECT_EQ(at(group, QPalette::BrightText), errorInk(sDark));
                EXPECT_EQ(at(group, QPalette::HighlightedText), sDark.mText);
                EXPECT_EQ(at(group, QPalette::Highlight), selectionWash(sDark));
                EXPECT_EQ(at(group, QPalette::Accent), sDark.mAccent);
                EXPECT_EQ(at(group, QPalette::Link), sDark.mAccent);
                EXPECT_EQ(at(group, QPalette::LinkVisited), sDark.mAccent);
            }

            EXPECT_EQ(at(QPalette::Disabled, QPalette::Window), sDark.mWindow);
            EXPECT_EQ(at(QPalette::Disabled, QPalette::Button), sDark.mElement);
            for (const QPalette::ColorRole ink : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                     QPalette::HighlightedText, QPalette::PlaceholderText })
                EXPECT_EQ(at(QPalette::Disabled, ink), sDark.mTextDisabled) << ink;
        }
    }
}
