#include "look.hpp"

#include <cassert>

#include <QApplication>
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
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
}
