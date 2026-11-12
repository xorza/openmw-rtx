#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QStyle>

#include <components/graphite/look.hpp>
#include <components/graphite/palette.hpp>
#include <components/graphite/style.hpp>

#include "offscreen.hpp"

namespace Graphite
{
    namespace
    {
        /// **The look is the style over Fusion, in the palette, its text at 16 px**, whatever the
        /// platform's own. The dark
        /// scheme it asks for is a request a platform's theme answers or ignores, and the offscreen one
        /// ignores it.
        TEST(GraphiteLookTest, theLookIsTheStyleOverFusionInThePalette)
        {
            offscreenApplication();
            apply();
            const auto* style = dynamic_cast<const Style*>(QApplication::style());
            ASSERT_NE(style, nullptr);
            EXPECT_EQ(style->baseStyle()->name(), QStringLiteral("fusion"));
            EXPECT_EQ(QApplication::palette(), qtPaletteOf(sDark));
            EXPECT_EQ(QApplication::font().pixelSize(), 16);
            EXPECT_EQ(QApplication::palette("QMenu").color(QPalette::Window).rgba(), sDark.mElement)
                << "a combo box's list rows the window's ground";
        }
    }
}
