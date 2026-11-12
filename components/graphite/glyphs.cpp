#include "glyphs.hpp"

#include <QByteArray>
#include <QColor>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include "palette.hpp"

namespace Graphite
{
    namespace
    {
        /// Lucide 0.460.0, ISC (`files/licenses/Lucide.txt`): each glyph's shapes, drawn on a 24 × 24 grid with
        /// a stroke of 2 in the ink the icon is made with.
        const char* shapesOf(Glyph glyph)
        {
            switch (glyph)
            {
                case Glyph::Question: // "circle-help"
                    return R"svg(<circle cx="12" cy="12" r="10" /><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3" /><path d="M12 17h.01" />)svg";
                case Glyph::Information: // "info"
                    return R"svg(<circle cx="12" cy="12" r="10" /><path d="M12 16v-4" /><path d="M12 8h.01" />)svg";
                case Glyph::Warning: // "triangle-alert"
                    return R"svg(<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3" /><path d="M12 9v4" /><path d="M12 17h.01" />)svg";
                case Glyph::Critical: // "circle-x"
                    return R"svg(<circle cx="12" cy="12" r="10" /><path d="m15 9-6 6" /><path d="m9 9 6 6" />)svg";
            }
            return "";
        }

        QByteArray svgOf(Glyph glyph, QRgb ink)
        {
            return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"")
                + QColor::fromRgb(ink).name().toLatin1()
                + "\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" + shapesOf(glyph)
                + "</svg>";
        }

        /// A glyph rendered from its SVG at the size and scale asked, so it stays sharp at any.
        class GlyphEngine final : public QIconEngine
        {
        public:
            GlyphEngine(Glyph glyph, QRgb ink)
                : mGlyph(glyph)
                , mInk(ink)
            {
            }

            void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
            {
                QSvgRenderer renderer(svgOf(mGlyph, mode == QIcon::Disabled ? sDark.mTextDisabled : mInk));
                renderer.render(painter, rect);
            }

            QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
            {
                return scaledPixmap(size, mode, state, 1.0);
            }

            QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
            {
                QPixmap pixmap(size * scale);
                pixmap.fill(Qt::transparent);
                {
                    QPainter painter(&pixmap);
                    painter.setRenderHint(QPainter::Antialiasing);
                    paint(&painter, QRect(QPoint(0, 0), pixmap.size()), mode, state);
                }
                pixmap.setDevicePixelRatio(scale);
                return pixmap;
            }

            QIconEngine* clone() const override { return new GlyphEngine(mGlyph, mInk); }

        private:
            const Glyph mGlyph;
            const QRgb mInk;
        };
    }

    QIcon glyphIcon(Glyph glyph, QRgb ink)
    {
        // The icon takes the engine and deletes it with itself.
        return QIcon(new GlyphEngine(glyph, ink));
    }
}
