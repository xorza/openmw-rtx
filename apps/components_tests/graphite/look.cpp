#include <gtest/gtest.h>

#include <QApplication>
#include <QStyle>

#include <components/graphite/look.hpp>
#include <components/graphite/palette.hpp>

#include "offscreen.hpp"

namespace Graphite
{
    namespace
    {
        /// **The look is Fusion in the palette**, whatever the platform's own. The dark scheme it asks for
        /// is a request a platform's theme answers or ignores, and the offscreen one ignores it.
        TEST(GraphiteLookTest, theLookIsFusionInThePalette)
        {
            offscreenApplication();
            apply();
            EXPECT_EQ(QApplication::style()->name(), QStringLiteral("fusion"));
            EXPECT_EQ(QApplication::palette(), qtPaletteOf(sDark));
        }
    }
}
