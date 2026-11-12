#include "look.hpp"

#include <cassert>

#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>

#include "palette.hpp"

namespace Graphite
{
    void apply()
    {
        assert(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr && "a look with no application");
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(qtPaletteOf(sDark));
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
}
