#include "style.hpp"

#include <algorithm>
#include <array>
#include <tuple>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>

#include "palette.hpp"

namespace Graphite
{
    namespace
    {
        /// palantir's `Metrics::DEFAULT`, the steps a recipe here reads: a control's corner and the line
        /// widths, in logical pixels.
        constexpr qreal sControlRadius = 4.0;
        constexpr qreal sCardRadius = 6.0;
        constexpr qreal sHairline = 1.0;
        constexpr qreal sFine = 1.5;
        constexpr qreal sLine = 2.0;

        /// palantir's toggle box and combo chevron.
        constexpr int sToggleSide = 16;
        constexpr qreal sChevronWidth = 10.0;
        constexpr qreal sChevronHeight = 6.0;

        /// palantir's scroll bar: an 8 px pill, and the gap of 4 between it and what it scrolls.
        constexpr int sScrollThickness = 8;
        constexpr int sScrollGap = 4;
        constexpr int sScrollThumbLeast = 24;

        /// palantir's row padding above and below a control's text, `space.sm`.
        constexpr int sRowPadding = 8;

        /// Where a field's text starts, palantir's field inset `space.sm`, and the 2 px `QLineEdit` and a
        /// combo box's label already add inside the rectangle the style hands them.
        constexpr int sFieldInset = 8;
        constexpr int sEditMargin = 2;
        constexpr int sLabelMargin = 1;

        /// A spin box's column of two arrows, and the chevron each carries.
        constexpr int sSpinColumn = 20;
        constexpr qreal sSpinChevronWidth = 8.0;
        constexpr qreal sSpinChevronHeight = 4.0;

        /// palantir's menu padding, `space.xs`, above its first row and below its last.
        constexpr int sMenuPadding = 4;

        /// A menu row's inset from its menu's sides, palantir's `space.xs`.
        constexpr int sMenuRowInset = 4;

        constexpr QRgb sNone = qRgba(0, 0, 0, 0);

        QRgb withAlpha(QRgb colour, int alpha)
        {
            return qRgba(qRed(colour), qGreen(colour), qBlue(colour), alpha);
        }

        /// What a surface wears: its fill and, inside its outline, an edge of a width.
        struct Chrome
        {
            QRgb mFill;
            QRgb mEdge;
            qreal mEdgeWidth = sHairline;
        };

        bool has(const QStyleOption& option, QStyle::StateFlag flag)
        {
            return option.state.testFlag(flag);
        }

        /// Whether the focus came from the keyboard, where palantir draws its ring: a click focuses a
        /// control without one.
        bool keyboardFocused(const QStyleOption& option)
        {
            return has(option, QStyle::State_HasFocus) && has(option, QStyle::State_KeyboardFocusChange);
        }

        /// `chrome` over `rect` with corners of `radius`: the fill over the whole shape, then the edge as
        /// the ring between the outline and the outline inset by its width, so a straight edge covers
        /// whole pixels at any scale and reads as the edge over the fill.
        void paint(QPainter& painter, const QRectF& rect, qreal radius, const Chrome& chrome)
        {
            const QPainterStateGuard guard(&painter);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);

            QPainterPath outline;
            outline.addRoundedRect(rect, radius, radius);
            painter.fillPath(outline, QColor::fromRgba(chrome.mFill));

            const qreal inset = chrome.mEdgeWidth;
            QPainterPath ring = outline;
            ring.addRoundedRect(rect.adjusted(inset, inset, -inset, -inset), std::max(radius - inset, 0.0),
                std::max(radius - inset, 0.0));
            ring.setFillRule(Qt::OddEvenFill);
            painter.fillPath(ring, QColor::fromRgba(chrome.mEdge));
        }

        /// A push button, palantir's `ButtonTheme`: resting on `mElementMid`, `mElementStrong` under the
        /// pointer, pressed with the focused edge, disabled a rung down; the default button filled with the
        /// accent, which moves toward white under the pointer and pressed. A keyboard focus is the
        /// accent's ring.
        Chrome buttonChrome(const QStyleOption& option, bool isDefault)
        {
            const Palette& p = sDark;
            if (!has(option, QStyle::State_Enabled))
                return { p.mElement, borderSoft(p) };

            const bool pressed = has(option, QStyle::State_Sunken) || has(option, QStyle::State_On);
            const bool hovered = has(option, QStyle::State_MouseOver);
            Chrome chrome = [&]() -> Chrome {
                if (isDefault)
                {
                    const QRgb fill = pressed ? accentPressed(p) : hovered ? accentHovered(p) : p.mAccent;
                    return { fill, fill };
                }
                if (pressed)
                    return { p.mElementStrong, p.mBorderFocused };
                return { hovered ? p.mElementStrong : p.mElementMid, borderSoft(p) };
            }();
            if (keyboardFocused(option))
                chrome = { chrome.mFill, isDefault ? p.mOnAccent : p.mBorderFocused, sLine };
            return chrome;
        }

        /// A tool button that rises only under the pointer, palantir's subtle button: nothing at rest,
        /// `mElementMid` under the pointer or checked, `mElementStrong` pressed.
        Chrome subtleChrome(const QStyleOption& option)
        {
            const Palette& p = sDark;
            if (!has(option, QStyle::State_Enabled))
                return { sNone, sNone };
            const QRgb edge = keyboardFocused(option) ? p.mBorderFocused : sNone;
            const qreal width = keyboardFocused(option) ? sLine : sHairline;
            if (has(option, QStyle::State_Sunken))
                return { p.mElementStrong, edge, width };
            if (has(option, QStyle::State_MouseOver) || has(option, QStyle::State_On))
                return { p.mElementMid, edge, width };
            return { sNone, edge, width };
        }

        /// A field, palantir's `TextEditTheme`: `mElementMid` with a soft edge that turns the focused one
        /// when focused, and never changes width; a rung down when disabled.
        Chrome fieldChrome(const QStyleOption& option)
        {
            const Palette& p = sDark;
            if (!has(option, QStyle::State_Enabled))
                return { p.mElement, borderSoft(p) };
            return { p.mElementMid, has(option, QStyle::State_HasFocus) ? p.mBorderFocused : borderSoft(p) };
        }

        /// A check box's or a radio's box, palantir's `ToggleTheme`: off on `mElementMid` behind the
        /// strong edge, on filled with the accent, the focused edge when pressed or focused from the keys.
        Chrome toggleChrome(const QStyleOption& option, bool on)
        {
            const Palette& p = sDark;
            const bool enabled = has(option, QStyle::State_Enabled);
            const bool emphasised = has(option, QStyle::State_Sunken) || keyboardFocused(option);
            if (on)
            {
                if (!enabled)
                    return { withAlpha(p.mAccent, 115), sNone };
                return { p.mAccent, emphasised ? p.mBorderFocused : p.mAccent };
            }
            if (!enabled)
                return { p.mElement, borderSoft(p) };
            if (emphasised)
                return { p.mElementStrong, p.mBorderFocused };
            return { has(option, QStyle::State_MouseOver) ? p.mElementStrong : p.mElementMid, borderStrong(p) };
        }

        /// The ink a glyph on a control is drawn in.
        QRgb glyphInk(const QStyleOption& option)
        {
            return has(option, QStyle::State_Enabled) ? sDark.mText : sDark.mTextDisabled;
        }

        enum class Pointing
        {
            Up,
            Down,
            Left,
            Right,
        };

        /// palantir's chevron: three points, the middle one the tip, a fine stroke, centred in `box`.
        void chevron(QPainter& painter, const QRectF& box, Pointing pointing, QRgb ink, qreal width = sChevronWidth,
            qreal height = sChevronHeight)
        {
            const QPointF centre = box.center();
            const bool across = pointing == Pointing::Up || pointing == Pointing::Down;
            const qreal halfWide = (across ? width : height) / 2.0;
            const qreal halfTall = (across ? height : width) / 2.0;
            std::array<QPointF, 3> points;
            switch (pointing)
            {
                case Pointing::Down:
                    points = { centre + QPointF(-halfWide, -halfTall), centre + QPointF(0.0, halfTall),
                        centre + QPointF(halfWide, -halfTall) };
                    break;
                case Pointing::Up:
                    points = { centre + QPointF(-halfWide, halfTall), centre + QPointF(0.0, -halfTall),
                        centre + QPointF(halfWide, halfTall) };
                    break;
                case Pointing::Right:
                    points = { centre + QPointF(-halfWide, -halfTall), centre + QPointF(halfWide, 0.0),
                        centre + QPointF(-halfWide, halfTall) };
                    break;
                case Pointing::Left:
                    points = { centre + QPointF(halfWide, -halfTall), centre + QPointF(-halfWide, 0.0),
                        centre + QPointF(halfWide, halfTall) };
                    break;
            }
            const QPainterStateGuard guard(&painter);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen pen(QColor::fromRgba(ink), sFine);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(points.data(), static_cast<int>(points.size()));
        }

        /// The check box's mark, palantir's two strokes in a box of `sToggleSide`, or the dash a partly
        /// checked box shows.
        void checkMark(QPainter& painter, const QRectF& box, bool partly, QRgb ink)
        {
            const qreal scale = box.width() / sToggleSide;
            const QPainterStateGuard guard(&painter);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen pen(QColor::fromRgba(ink), sLine);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            if (partly)
            {
                painter.drawLine(box.topLeft() + QPointF(4.0, 8.0) * scale, box.topLeft() + QPointF(12.0, 8.0) * scale);
                return;
            }
            const std::array<QPointF, 3> points{ box.topLeft() + QPointF(4.0, 8.5) * scale,
                box.topLeft() + QPointF(7.0, 11.5) * scale, box.topLeft() + QPointF(12.0, 5.0) * scale };
            painter.drawPolyline(points.data(), static_cast<int>(points.size()));
        }

        /// A control's height: its text, `sRowPadding` above and below, and a hairline each side.
        int rowHeight(const QStyleOption& option)
        {
            return option.fontMetrics.height() + 2 * sRowPadding + 2 * static_cast<int>(sHairline);
        }

        /// A spin box's parts: the field from its inset, and on the far side a column of `sSpinColumn` split
        /// into the up arrow above the down, or the field alone where the box shows no arrows.
        QRect spinBoxRect(const QStyleOptionComplex& option, QStyle::SubControl part)
        {
            const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(&option);
            const bool arrows = spin == nullptr || spin->buttonSymbols != QAbstractSpinBox::NoButtons;
            const QRect& rect = option.rect;
            const int column = arrows ? sSpinColumn : 0;
            const int half = rect.height() / 2;
            QRect laidOut;
            switch (part)
            {
                case QStyle::SC_SpinBoxFrame:
                    return rect;
                case QStyle::SC_SpinBoxEditField:
                    laidOut = QRect(rect.left() + sFieldInset - sEditMargin, rect.top() + 1,
                        rect.width() - column - (sFieldInset - sEditMargin), rect.height() - 2);
                    break;
                case QStyle::SC_SpinBoxUp:
                    if (!arrows)
                        return {};
                    laidOut = QRect(rect.right() + 1 - column, rect.top(), column, half);
                    break;
                case QStyle::SC_SpinBoxDown:
                    if (!arrows)
                        return {};
                    laidOut = QRect(rect.right() + 1 - column, rect.top() + half, column, rect.height() - half);
                    break;
                default:
                    return {};
            }
            return QStyle::visualRect(option.direction, rect, laidOut);
        }

        /// The square of `sToggleSide` centred in `rect`, whatever size the caller laid out.
        QRectF toggleBox(const QRect& rect)
        {
            const QPointF centre = QRectF(rect).center();
            return { centre.x() - sToggleSide / 2.0, centre.y() - sToggleSide / 2.0, qreal(sToggleSide),
                qreal(sToggleSide) };
        }
    }

    Style::Style()
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
    {
    }

    void Style::drawPrimitive(
        PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
    {
        switch (element)
        {
            case PE_PanelButtonCommand:
            {
                const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
                const bool isDefault
                    = button != nullptr && button->features.testFlag(QStyleOptionButton::DefaultButton);
                paint(*painter, option->rect, sControlRadius, buttonChrome(*option, isDefault));
                return;
            }
            case PE_PanelButtonTool:
                paint(*painter, option->rect, sControlRadius,
                    has(*option, State_AutoRaise) ? subtleChrome(*option) : buttonChrome(*option, false));
                return;
            case PE_PanelLineEdit:
            {
                // A field with no frame is one a spin box or an editable combo box holds, whose own fill
                // stands behind it.
                const auto* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
                if (frame != nullptr && frame->lineWidth > 0)
                    paint(*painter, option->rect, sControlRadius, fieldChrome(*option));
                return;
            }
            case PE_FrameLineEdit:
            {
                Chrome edgeOnly = fieldChrome(*option);
                edgeOnly.mFill = sNone;
                paint(*painter, option->rect, sControlRadius, edgeOnly);
                return;
            }
            case PE_IndicatorCheckBox:
            {
                const bool on = has(*option, State_On);
                const bool partly = has(*option, State_NoChange);
                const QRectF box = toggleBox(option->rect);
                paint(*painter, box, sControlRadius, toggleChrome(*option, on || partly));
                if (on || partly)
                    checkMark(*painter, box, partly, sDark.mOnAccent);
                return;
            }
            case PE_IndicatorRadioButton:
            {
                const bool on = has(*option, State_On);
                const QRectF box = toggleBox(option->rect);
                paint(*painter, box, sToggleSide / 2.0, toggleChrome(*option, on));
                if (on)
                {
                    const QPainterStateGuard guard(painter);
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor::fromRgba(sDark.mOnAccent));
                    painter->drawEllipse(box.center(), 3.0, 3.0);
                }
                return;
            }
            case PE_IndicatorArrowUp:
            case PE_IndicatorSpinUp:
            case PE_IndicatorSpinPlus:
                chevron(*painter, option->rect, Pointing::Up, glyphInk(*option));
                return;
            case PE_IndicatorArrowDown:
            case PE_IndicatorSpinDown:
            case PE_IndicatorSpinMinus:
                chevron(*painter, option->rect, Pointing::Down, glyphInk(*option));
                return;
            case PE_IndicatorArrowLeft:
                chevron(*painter, option->rect, Pointing::Left, glyphInk(*option));
                return;
            case PE_IndicatorArrowRight:
                chevron(*painter, option->rect, Pointing::Right, glyphInk(*option));
                return;
            case PE_IndicatorHeaderArrow:
            {
                const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
                if (header != nullptr && header->sortIndicator != QStyleOptionHeader::None)
                    chevron(*painter, option->rect,
                        header->sortIndicator == QStyleOptionHeader::SortUp ? Pointing::Up : Pointing::Down,
                        glyphInk(*option));
                return;
            }
            // A tab widget's pane is the window's ground, as palantir's selected tab joins the content
            // under its strip, behind a soft edge.
            case PE_FrameTabWidget:
                paint(*painter, option->rect, sCardRadius, { sDark.mWindow, borderSoft(sDark) });
                return;
            case PE_FrameTabBarBase:
                return;
            case PE_FrameGroupBox:
                paint(*painter, option->rect, sCardRadius, { sNone, borderSoft(sDark) });
                return;
            // A view's frame and a text area's: the soft edge, square, as the viewport inside it is.
            case PE_Frame:
                paint(*painter, option->rect, 0.0, { sNone, borderSoft(sDark) });
                return;
            case PE_PanelScrollAreaCorner:
                painter->fillRect(option->rect, QColor::fromRgba(sDark.mWindow));
                return;
            // A menu and a tooltip are palantir's floating panel, `mElement` behind the middle edge. Square:
            // a rounded top-level window is a translucent one, which a desk without a compositor draws black.
            case PE_PanelMenu:
            case PE_PanelTipLabel:
                paint(*painter, option->rect, 0.0, { sDark.mElement, borderMid(sDark) });
                return;
            case PE_FrameMenu:
                paint(*painter, option->rect, 0.0, { sNone, borderMid(sDark) });
                return;
            // A view's row: the selection's tint, or `mElementMid` under the pointer.
            case PE_PanelItemViewItem:
            {
                if (has(*option, State_Selected))
                    painter->fillRect(option->rect, QColor::fromRgba(selectionWash(sDark)));
                else if (has(*option, State_MouseOver) && has(*option, State_Enabled))
                    painter->fillRect(option->rect, QColor::fromRgba(sDark.mElementMid));
                return;
            }
            // A focused control says so by its edge, as palantir's do, and a default button by its fill.
            case PE_FrameFocusRect:
            case PE_FrameDefaultButton:
                return;
            default:
                QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
    }

    void Style::drawControl(
        ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
    {
        switch (element)
        {
            case CE_TabBarTabShape:
            {
                const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
                if (tab == nullptr || (tab->shape != QTabBar::RoundedNorth && tab->shape != QTabBar::TriangularNorth))
                    break;
                // palantir's tab chip: the selected one the window's ground, joining the pane under it,
                // with the accent's cap; another a ghost that fills under the pointer.
                const QRectF rect = option->rect;
                const QPainterStateGuard guard(painter);
                painter->setRenderHint(QPainter::Antialiasing);
                QPainterPath chip;
                chip.addRoundedRect(rect.adjusted(0, 0, 0, sCardRadius), sCardRadius, sCardRadius);
                painter->setClipRect(rect);
                const bool selected = has(*option, State_Selected);
                if (selected)
                    painter->fillPath(chip, QColor::fromRgba(sDark.mWindow));
                else if (has(*option, State_MouseOver) && has(*option, State_Enabled))
                    painter->fillPath(chip, QColor::fromRgba(sDark.mElementMid));
                if (selected)
                    painter->fillRect(QRectF(rect.left(), rect.bottom() - sLine, rect.width(), sLine),
                        QColor::fromRgba(sDark.mAccent));
                return;
            }
            case CE_TabBarTabLabel:
            {
                const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
                if (tab == nullptr || has(*option, State_Selected) || has(*option, State_MouseOver)
                    || !has(*option, State_Enabled))
                    break;
                QStyleOptionTab muted = *tab;
                muted.palette.setColor(QPalette::WindowText, QColor::fromRgba(sDark.mTextMuted));
                muted.palette.setColor(QPalette::ButtonText, QColor::fromRgba(sDark.mTextMuted));
                QProxyStyle::drawControl(element, &muted, painter, widget);
                return;
            }
            case CE_ProgressBarGroove:
                paint(*painter, option->rect, sControlRadius, { sDark.mElementMid, sDark.mElementMid });
                return;
            case CE_ProgressBarContents:
            {
                const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
                // A bar with no range is busy, which Fusion animates.
                if (bar == nullptr || bar->maximum <= bar->minimum)
                    break;
                const qreal done = qreal(bar->progress - bar->minimum) / qreal(bar->maximum - bar->minimum);
                QRectF filled = option->rect;
                const bool horizontal = has(*option, State_Horizontal);
                const bool reversed = (bar->direction == Qt::RightToLeft) != bar->invertedAppearance;
                if (horizontal)
                {
                    const qreal width = filled.width() * std::clamp(done, 0.0, 1.0);
                    filled = reversed ? QRectF(filled.right() - width, filled.top(), width, filled.height())
                                      : QRectF(filled.left(), filled.top(), width, filled.height());
                }
                else
                {
                    const qreal height = filled.height() * std::clamp(done, 0.0, 1.0);
                    filled = bar->invertedAppearance
                        ? QRectF(filled.left(), filled.top(), filled.width(), height)
                        : QRectF(filled.left(), filled.bottom() - height, filled.width(), height);
                }
                if (!filled.isEmpty())
                    paint(*painter, filled, sControlRadius, { sDark.mAccent, sDark.mAccent });
                return;
            }
            // Over the fill the label is the accent's own ink, as on a default button.
            case CE_ProgressBarLabel:
            {
                const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
                if (bar == nullptr)
                    break;
                QStyleOptionProgressBar inked = *bar;
                inked.palette.setColor(QPalette::HighlightedText, QColor::fromRgba(sDark.mOnAccent));
                QProxyStyle::drawControl(element, &inked, painter, widget);
                return;
            }
            // A menu row under the pointer is palantir's: `mElementMid` inset from the menu's sides, under
            // the row's own ink, where Fusion fills the row with the selection's colour.
            case CE_MenuItem:
            {
                const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
                if (item == nullptr)
                    break;
                // A separator is palantir's: the soft edge, held off the menu's sides as its rows are.
                if (item->menuItemType == QStyleOptionMenuItem::Separator)
                {
                    const QRect rect = option->rect;
                    painter->fillRect(
                        QRect(rect.left() + sMenuRowInset, rect.center().y(), rect.width() - 2 * sMenuRowInset, 1),
                        QColor::fromRgba(borderSoft(sDark)));
                    return;
                }
                if (!has(*option, State_Selected))
                    break;
                if (has(*option, State_Enabled) && item->menuItemType != QStyleOptionMenuItem::Separator)
                    paint(*painter, QRectF(option->rect).adjusted(sMenuRowInset, 0, -sMenuRowInset, 0), sControlRadius,
                        { sDark.mElementMid, sDark.mElementMid });
                QStyleOptionMenuItem resting = *item;
                resting.state &= ~State_Selected;
                QProxyStyle::drawControl(element, &resting, painter, widget);
                return;
            }
            case CE_HeaderSection:
            {
                painter->fillRect(option->rect, QColor::fromRgba(sDark.mSurround));
                const QRect rect = option->rect;
                painter->fillRect(
                    QRect(rect.right(), rect.top(), 1, rect.height()), QColor::fromRgba(borderSoft(sDark)));
                painter->fillRect(
                    QRect(rect.left(), rect.bottom(), rect.width(), 1), QColor::fromRgba(borderSoft(sDark)));
                return;
            }
            default:
                break;
        }
        if (element == CE_PushButtonLabel)
        {
            const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
            if (button != nullptr && button->features.testFlag(QStyleOptionButton::DefaultButton)
                && has(*option, State_Enabled))
            {
                QStyleOptionButton inked = *button;
                inked.palette.setColor(QPalette::ButtonText, QColor::fromRgba(sDark.mOnAccent));
                QProxyStyle::drawControl(element, &inked, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void Style::drawComplexControl(
        ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
    {
        switch (control)
        {
            case CC_ComboBox:
            {
                const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
                if (combo == nullptr)
                    break;
                // An editable combo box is a field; one that only picks is a button, as palantir's combo
                // box wears its button's look.
                paint(*painter, option->rect, sControlRadius,
                    combo->editable ? fieldChrome(*option) : buttonChrome(*option, false));
                const QRect arrow = subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);
                chevron(*painter, arrow, Pointing::Down, glyphInk(*option));
                return;
            }
            case CC_ScrollBar:
            {
                // No track and no arrows: palantir's pill, `mTextMuted` at 115 of 255 at rest, at 166 under
                // the pointer and at 217 dragged.
                const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
                if (bar == nullptr)
                    break;
                const QRect thumb = subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, widget);
                if (thumb.isEmpty() || bar->minimum == bar->maximum)
                    return;
                const bool onThumb = bar->activeSubControls.testFlag(SC_ScrollBarSlider);
                const int alpha = onThumb && has(*option, State_Sunken) ? 217
                    : onThumb && has(*option, State_MouseOver)          ? 166
                                                                        : 115;
                const QRgb fill = withAlpha(sDark.mTextMuted, alpha);
                const qreal side = std::min(thumb.width(), thumb.height());
                paint(*painter, thumb, side / 2.0, { fill, sNone });
                return;
            }
            case CC_GroupBox:
            {
                const auto* group = qstyleoption_cast<const QStyleOptionGroupBox*>(option);
                if (group == nullptr)
                    break;
                QStyleOptionGroupBox muted = *group;
                muted.palette.setColor(QPalette::WindowText, QColor::fromRgba(sDark.mTextMuted));
                muted.textColor = QColor::fromRgba(sDark.mTextMuted);
                QProxyStyle::drawComplexControl(control, &muted, painter, widget);
                return;
            }
            case CC_SpinBox:
            {
                const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
                if (spin == nullptr)
                    break;
                paint(*painter, option->rect, sControlRadius, fieldChrome(*option));
                // The column's soft hairlines: one beside the field, one between its two arrows, each held off
                // the edges as palantir's separator is.
                if (spin->subControls.testFlag(SC_SpinBoxUp) && spin->subControls.testFlag(SC_SpinBoxDown))
                {
                    const QRect up = subControlRect(CC_SpinBox, option, SC_SpinBoxUp, widget);
                    const QRect down = subControlRect(CC_SpinBox, option, SC_SpinBoxDown, widget);
                    const QColor rule = QColor::fromRgba(borderSoft(sDark));
                    painter->fillRect(
                        QRect(up.left(), up.top() + sMenuPadding, 1, down.bottom() - up.top() - 2 * sMenuPadding + 1),
                        rule);
                    painter->fillRect(
                        QRect(up.left() + sMenuPadding, down.top(), up.width() - 2 * sMenuPadding, 1), rule);
                }
                for (const auto& [part, pointing, step] :
                    { std::tuple{ SC_SpinBoxUp, Pointing::Up, QAbstractSpinBox::StepUpEnabled },
                        std::tuple{ SC_SpinBoxDown, Pointing::Down, QAbstractSpinBox::StepDownEnabled } })
                {
                    if (!spin->subControls.testFlag(part))
                        continue;
                    const QRect rect = subControlRect(CC_SpinBox, option, part, widget);
                    const bool steps = has(*option, State_Enabled) && spin->stepEnabled.testFlag(step);
                    const bool active = steps && spin->activeSubControls.testFlag(part);
                    if (active && (has(*option, State_Sunken) || has(*option, State_MouseOver)))
                        paint(*painter, QRectF(rect).adjusted(2, 2, -2, -2), sControlRadius - 1,
                            { sDark.mElementStrong, sDark.mElementStrong });
                    chevron(*painter, rect, pointing, steps ? sDark.mText : sDark.mTextDisabled, sSpinChevronWidth,
                        sSpinChevronHeight);
                }
                return;
            }
            default:
                break;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

    int Style::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
    {
        switch (metric)
        {
            case PM_MenuVMargin:
                return sMenuPadding;
            case PM_ScrollBarExtent:
                return sScrollThickness + sScrollGap;
            case PM_ScrollBarSliderMin:
                return sScrollThumbLeast;
            case PM_IndicatorWidth:
            case PM_IndicatorHeight:
            case PM_ExclusiveIndicatorWidth:
            case PM_ExclusiveIndicatorHeight:
                return sToggleSide;
            default:
                return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    QRect Style::subControlRect(
        ComplexControl control, const QStyleOptionComplex* option, SubControl subControl, const QWidget* widget) const
    {
        if (control == CC_SpinBox)
            return spinBoxRect(*option, subControl);
        if (control == CC_ComboBox && subControl == SC_ComboBoxEditField)
        {
            // From the field's inset to the arrow, the text placed where a line edit's is.
            const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
            const QRect arrow = QProxyStyle::subControlRect(control, option, SC_ComboBoxArrow, widget);
            const int inset = sFieldInset - (combo != nullptr && combo->editable ? sEditMargin : sLabelMargin);
            const QRect laidOut = visualRect(option->direction, option->rect, arrow);
            const QRect field(option->rect.left() + inset, option->rect.top() + 1,
                laidOut.left() - option->rect.left() - inset, option->rect.height() - 2);
            return visualRect(option->direction, option->rect, field);
        }
        const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (control != CC_ScrollBar || bar == nullptr)
            return QProxyStyle::subControlRect(control, option, subControl, widget);

        // The common style's layout with no arrow buttons, as its transient bars have none, and the pill
        // on the far side of the gap from what the bar scrolls.
        const QRect& rect = option->rect;
        const bool horizontal = bar->orientation == Qt::Horizontal;
        const int length = horizontal ? rect.width() : rect.height();
        int thumbLength = length;
        if (bar->maximum != bar->minimum)
        {
            const qint64 range = qint64(bar->maximum) - bar->minimum;
            thumbLength = int(qint64(bar->pageStep) * length / (range + bar->pageStep));
            thumbLength = std::clamp(thumbLength, std::min(sScrollThumbLeast, length), length);
        }
        const int thumbStart = sliderPositionFromValue(
            bar->minimum, bar->maximum, bar->sliderPosition, length - thumbLength, bar->upsideDown);

        const auto across = [&](int start, int extent) {
            return horizontal ? QRect(rect.left() + start, rect.top() + sScrollGap, extent, sScrollThickness)
                              : QRect(rect.left() + sScrollGap, rect.top() + start, sScrollThickness, extent);
        };
        switch (subControl)
        {
            case SC_ScrollBarSlider:
                return across(thumbStart, thumbLength);
            case SC_ScrollBarSubPage:
                return across(0, thumbStart);
            case SC_ScrollBarAddPage:
                return across(thumbStart + thumbLength, length - thumbStart - thumbLength);
            case SC_ScrollBarGroove:
                return rect;
            default:
                return {};
        }
    }

    void Style::polish(QWidget* widget)
    {
        QProxyStyle::polish(widget);
        // A combo box's list is palantir's dropdown, a menu: its popup draws the menu's panel, and the list
        // inside leaves it showing, where a view fills itself with the window's ground.
        if (auto* combo = qobject_cast<QComboBox*>(widget))
        {
            combo->view()->viewport()->setAutoFillBackground(false);
            combo->view()->setFrameShape(QFrame::NoFrame);
        }
    }

    QRect Style::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
    {
        // A field's text starts at the field's inset; one with no frame lies in a spin box's or a combo
        // box's field, which placed it.
        const auto* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
        if (element == SE_LineEditContents && frame != nullptr && frame->lineWidth > 0)
        {
            const int inset = sFieldInset - sEditMargin;
            return option->rect.adjusted(inset, 1, -inset, -1);
        }
        return QProxyStyle::subElementRect(element, option, widget);
    }

    QSize Style::sizeFromContents(
        ContentsType type, const QStyleOption* option, const QSize& contents, const QWidget* widget) const
    {
        QSize size = QProxyStyle::sizeFromContents(type, option, contents, widget);
        switch (type)
        {
            // One row's height for a button and the fields beside it: the text, palantir's row padding
            // above and below, and the hairline edge.
            case CT_ComboBox:
            case CT_SpinBox:
            {
                // As wide again as the field's inset and the spin box's column take from Fusion's edit field,
                // so the text Fusion sized it for still fits.
                // Each laid out as its own option, whose subclass the edit field's rectangle reads.
                const auto widen = [&]<class Option>(const Option* control, ComplexControl kind, SubControl field) {
                    if (control == nullptr)
                        return;
                    Option laidOut = *control;
                    laidOut.rect = QRect(QPoint(0, 0), size);
                    const int fusion = baseStyle()->subControlRect(kind, &laidOut, field, widget).width();
                    const int ours = subControlRect(kind, &laidOut, field, widget).width();
                    size.rwidth() += std::max(0, fusion - ours);
                };
                if (type == CT_ComboBox)
                    widen(qstyleoption_cast<const QStyleOptionComboBox*>(option), CC_ComboBox, SC_ComboBoxEditField);
                else
                    widen(qstyleoption_cast<const QStyleOptionSpinBox*>(option), CC_SpinBox, SC_SpinBoxEditField);
                size.setHeight(std::max(size.height(), rowHeight(*option)));
                return size;
            }
            case CT_PushButton:
            case CT_LineEdit:
                size.setHeight(std::max(size.height(), rowHeight(*option)));
                return size;
            default:
                return size;
        }
    }

    int Style::styleHint(
        StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
    {
        switch (hint)
        {
            // Disabled text is the disabled ink, never dithered or etched.
            case SH_DitherDisabledText:
            case SH_EtchDisabledText:
            // A dialog's buttons are their words, as palantir's are: the platform's stock icons beside them
            // are another look's.
            case SH_DialogButtonBox_ButtonsHaveIcons:
                return 0;
            default:
                return QProxyStyle::styleHint(hint, option, widget, returnData);
        }
    }
}
