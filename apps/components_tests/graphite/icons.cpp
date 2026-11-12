#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QColor>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>

#include <components/files/conversion.hpp>
#include <components/graphite/look.hpp>
#include <components/graphite/palette.hpp>
#include <components/misc/scalableicon.hpp>

#include "offscreen.hpp"

namespace Graphite
{
    namespace
    {
        const std::filesystem::path sIcons = std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files/launcher/icons";

        QString contentOf(const std::filesystem::path& file)
        {
            QFile read(QString::fromStdString(Files::pathToUnicodeString(file)));
            EXPECT_TRUE(read.open(QIODevice::ReadOnly)) << file;
            return QString::fromUtf8(read.readAll());
        }

        /// Every colour an SVG names, as `#rrggbb`.
        std::vector<std::string> coloursOf(const QString& svg)
        {
            std::vector<std::string> colours;
            const QRegularExpression colour(QStringLiteral("#[0-9a-fA-F]{6}\\b"));
            for (auto match = colour.globalMatch(svg); match.hasNext();)
                colours.push_back(match.next().captured().toLower().toStdString());
            return colours;
        }

        std::string hexOf(QRgb colour)
        {
            return QColor::fromRgb(colour).name().toStdString();
        }

        /// **The fallback theme's glyphs are drawn in the palette's ink**, which `QIcon::fromTheme` draws
        /// as it is: a copy of `mText` the test holds to the table.
        TEST(GraphiteIconsTest, theFallbackGlyphsAreDrawnInThePalettesInk)
        {
            for (const char* name : { "document-new", "edit-clear", "edit-copy", "edit-delete", "view-refresh" })
                EXPECT_EQ(coloursOf(contentOf(sIcons / "fallback/16x16" / (std::string(name) + ".svg"))),
                    std::vector<std::string>{ hexOf(sDark.mText) })
                    << name;
        }

        /// **The toolbar's pages, the data mark and the wizard's icons are templates `Misc::ScalableIcon`
        /// recolours**, in its own ink, so they draw in the palette's text whatever it is: every solid pixel
        /// of one drawn under the look is `mText`.
        TEST(GraphiteIconsTest, theTemplateIconsAreOnesTheScalableIconRecolours)
        {
            offscreenApplication();
            apply();
            const std::filesystem::path wizard
                = std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files/wizard/icons";
            for (const std::filesystem::path& file :
                { sIcons / "pages/data.svg", sIcons / "pages/display.svg", sIcons / "pages/settings.svg",
                    sIcons / "pages/import.svg", sIcons / "marks/contains-data.svg", wizard / "folder.svg",
                    wizard / "dollar.svg", wizard / "system-installer.svg", wizard / "preferences-desktop-locale.svg" })
            {
                const std::string name = Files::pathToUnicodeString(file.filename());
                const QString svg = contentOf(file);
                EXPECT_TRUE(svg.startsWith(QStringLiteral("<?xml"))) << name << ": the icon loads as a plain file";
                EXPECT_EQ(coloursOf(svg), std::vector<std::string>{ "#4d4d4d" }) << name;

                const QImage drawn = Misc::ScalableIcon::load(QString::fromStdString(Files::pathToUnicodeString(file)))
                                         .pixmap(48, 48)
                                         .toImage()
                                         .convertToFormat(QImage::Format_ARGB32);
                int solid = 0;
                for (int y = 0; y < drawn.height(); ++y)
                    for (int x = 0; x < drawn.width(); ++x)
                        if (qAlpha(drawn.pixel(x, y)) == 255)
                        {
                            ++solid;
                            EXPECT_EQ(drawn.pixel(x, y), sDark.mText) << name << " at " << x << ", " << y;
                        }
                EXPECT_GT(solid, 0) << name << ": nothing drawn";
            }
        }
    }
}
