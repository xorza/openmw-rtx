#include "look.hpp"

#include <cassert>

#include <QApplication>
#include <QFont>
#include <QStyleHints>

#include "palette.hpp"
#include "style.hpp"

namespace Graphite
{
    void apply()
    {
        assert(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr && "a look with no application");
        // The application takes the style and deletes it with itself.
        QApplication::setStyle(new Style);
        QApplication::setPalette(qtPaletteOf(sDark));
        QFont font = QApplication::font();
        font.setPixelSize(sBodyPixels);
        QApplication::setFont(font);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
}
