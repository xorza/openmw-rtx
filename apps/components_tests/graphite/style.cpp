#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QMargins>
#include <QPainter>
#include <QRgb>
#include <QStyleOption>
#include <QTabBar>

#include <components/graphite/palette.hpp>
#include <components/graphite/style.hpp>

#include "offscreen.hpp"

namespace Graphite
{
    namespace
    {
        constexpr int sWidth = 80;
        constexpr int sHeight = 24;

        /// What `draw` paints over the window's ground, read back: a pixel of it is what the screen shows.
        /// A ground no recipe paints, so a pixel a recipe leaves alone reads as left alone.
        constexpr QRgb sGround = qRgb(0x10, 0x20, 0x30);

        QImage rendered(const std::function<void(QPainter&)>& draw, int width = sWidth, int height = sHeight,
            QRgb ground = sDark.mWindow)
        {
            QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromRgba(ground));
            QPainter painter(&image);
            draw(painter);
            painter.end();
            return image;
        }

        /// `top` over the opaque `under`, as `QPainter` blends it: the colour an edge over a fill reads.
        QRgb blended(QRgb top, QRgb under)
        {
            QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromRgba(under));
            QPainter painter(&image);
            painter.fillRect(0, 0, 1, 1, QColor::fromRgba(top));
            painter.end();
            return image.pixel(0, 0);
        }

        template <class Option>
        Option optionOf(QStyle::State state, QRect rect = QRect(0, 0, sWidth, sHeight))
        {
            Option option;
            option.state = state;
            option.rect = rect;
            option.palette = qtPaletteOf(sDark);
            return option;
        }

        constexpr QStyle::State sEnabled = QStyle::State_Enabled;

        /// The middle of the left edge, past the corner's curve, and the middle of the control.
        constexpr QPoint sEdge(0, sHeight / 2);
        constexpr QPoint sMiddle(sWidth / 2, sHeight / 2);

        class GraphiteStyleTest : public ::testing::Test
        {
        protected:
            void SetUp() override { offscreenApplication(); }

            QImage primitive(QStyle::PrimitiveElement element, const QStyleOption& option)
            {
                return rendered([&](QPainter& painter) { mStyle.drawPrimitive(element, &option, &painter, nullptr); });
            }

            Style mStyle;
        };

        /// **A push button is palantir's**: `#454545` at rest behind the soft edge, `#525252` under the
        /// pointer, pressed with the accent's edge, a rung down disabled, and the accent's ring of two
        /// pixels when the keyboard focused it.
        TEST_F(GraphiteStyleTest, aPushButtonRestsHoversPressesAndFocusesAsPalantirs)
        {
            const auto at = [&](QStyle::State state) {
                return primitive(QStyle::PE_PanelButtonCommand, optionOf<QStyleOptionButton>(state));
            };
            const QImage rest = at(sEnabled);
            EXPECT_EQ(rest.pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(rest.pixel(sEdge), blended(borderSoft(sDark), sDark.mElementMid));
            EXPECT_EQ(rest.pixel(1, sHeight / 2), sDark.mElementMid) << "an edge wider than a hairline";

            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver).pixel(sMiddle), sDark.mElementStrong);

            const QImage pressed = at(sEnabled | QStyle::State_Sunken);
            EXPECT_EQ(pressed.pixel(sMiddle), sDark.mElementStrong);
            EXPECT_EQ(pressed.pixel(sEdge), sDark.mBorderFocused);

            const QImage disabled = at(QStyle::State_None);
            EXPECT_EQ(disabled.pixel(sMiddle), sDark.mElement);

            const QImage focused = at(sEnabled | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
            EXPECT_EQ(focused.pixel(0, sHeight / 2), sDark.mBorderFocused);
            EXPECT_EQ(focused.pixel(1, sHeight / 2), sDark.mBorderFocused);
            EXPECT_EQ(focused.pixel(2, sHeight / 2), sDark.mElementMid);

            const QImage clicked = at(sEnabled | QStyle::State_HasFocus);
            EXPECT_EQ(clicked.pixel(sEdge), blended(borderSoft(sDark), sDark.mElementMid)) << "a ring for a click";
        }

        /// **The default button is the accent's**, which moves toward white under the pointer and
        /// further pressed, its label in the accent's own ink.
        TEST_F(GraphiteStyleTest, theDefaultButtonIsFilledWithTheAccent)
        {
            const auto at = [&](QStyle::State state) {
                QStyleOptionButton option = optionOf<QStyleOptionButton>(state);
                option.features = QStyleOptionButton::DefaultButton;
                return primitive(QStyle::PE_PanelButtonCommand, option);
            };
            EXPECT_EQ(at(sEnabled).pixel(sMiddle), sDark.mAccent);
            EXPECT_EQ(at(sEnabled).pixel(sEdge), sDark.mAccent);
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver).pixel(sMiddle), accentHovered(sDark));
            EXPECT_EQ(at(sEnabled | QStyle::State_Sunken).pixel(sMiddle), accentPressed(sDark));
            EXPECT_EQ(at(QStyle::State_None).pixel(sMiddle), sDark.mElement);
        }

        /// **A tool button that rises only under the pointer is invisible at rest**, `#454545` under the
        /// pointer and checked, `#525252` pressed; one that does not rise is a push button.
        TEST_F(GraphiteStyleTest, aRisingToolButtonShowsOnlyUnderThePointerOrChecked)
        {
            const auto at = [&](QStyle::State state) {
                return primitive(QStyle::PE_PanelButtonTool, optionOf<QStyleOptionToolButton>(state));
            };
            const QStyle::State rising = sEnabled | QStyle::State_AutoRaise;
            EXPECT_EQ(at(rising).pixel(sMiddle), sDark.mWindow);
            EXPECT_EQ(at(rising).pixel(sEdge), sDark.mWindow);
            EXPECT_EQ(at(rising | QStyle::State_MouseOver).pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(at(rising | QStyle::State_On).pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(at(rising | QStyle::State_Sunken).pixel(sMiddle), sDark.mElementStrong);
            EXPECT_EQ(at(sEnabled).pixel(sMiddle), sDark.mElementMid);

            // The page in use: the accent's pill at its foot, held off its sides by the corner.
            const QImage page = at(rising | QStyle::State_On);
            EXPECT_EQ(page.pixel(sWidth / 2, sHeight - 1), sDark.mAccent);
            EXPECT_EQ(page.pixel(sWidth / 2, sHeight - 4), sDark.mElementMid);
            EXPECT_EQ(at(rising | QStyle::State_MouseOver).pixel(sWidth / 2, sHeight - 1), sDark.mElementMid)
                << "a pill on a page not in use";
        }

        /// **A field is `#454545` behind the soft edge, which the focus turns the accent's at the same
        /// width**; one without a frame, inside a spin box or a combo box, paints nothing over the fill
        /// behind it.
        TEST_F(GraphiteStyleTest, aFieldsEdgeTurnsTheAccentsWhenFocused)
        {
            const auto at = [&](QStyle::State state, int lineWidth) {
                QStyleOptionFrame option = optionOf<QStyleOptionFrame>(state);
                option.lineWidth = lineWidth;
                return primitive(QStyle::PE_PanelLineEdit, option);
            };
            EXPECT_EQ(at(sEnabled, 1).pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(at(sEnabled, 1).pixel(sEdge), blended(borderSoft(sDark), sDark.mElementMid));
            const QImage focused = at(sEnabled | QStyle::State_HasFocus, 1);
            EXPECT_EQ(focused.pixel(sEdge), sDark.mBorderFocused);
            EXPECT_EQ(focused.pixel(1, sHeight / 2), sDark.mElementMid);
            EXPECT_EQ(at(QStyle::State_None, 1).pixel(sMiddle), sDark.mElement);
            EXPECT_EQ(at(sEnabled, 0).pixel(sMiddle), sDark.mWindow);
        }

        /// **A check box is palantir's toggle**: a 16 px box on `#454545` behind the strong edge, the
        /// accent's fill when checked, carrying its dark mark through the middle, and the accent at 115
        /// of 255 when checked and disabled. Read at a corner the mark never crosses.
        TEST_F(GraphiteStyleTest, aCheckBoxIsPalantirsToggle)
        {
            const QRect box(0, 0, 16, 16);
            const auto at = [&](QStyle::State state) {
                QStyleOptionButton option = optionOf<QStyleOptionButton>(state, box);
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, nullptr);
                    },
                    16, 16);
            };
            const QPoint inside(12, 13);
            const QImage off = at(sEnabled | QStyle::State_Off);
            EXPECT_EQ(off.pixel(inside), sDark.mElementMid);
            EXPECT_EQ(off.pixel(0, 8), blended(borderStrong(sDark), sDark.mElementMid));
            EXPECT_EQ(at(sEnabled | QStyle::State_Off | QStyle::State_MouseOver).pixel(inside), sDark.mElementStrong);

            const QImage on = at(sEnabled | QStyle::State_On);
            EXPECT_EQ(on.pixel(inside), sDark.mAccent);
            EXPECT_EQ(on.pixel(0, 8), sDark.mAccent);
            EXPECT_EQ(on.pixel(7, 11), sDark.mOnAccent) << "the mark's elbow";

            EXPECT_EQ(at(QStyle::State_On).pixel(inside),
                blended(qRgba(qRed(sDark.mAccent), qGreen(sDark.mAccent), qBlue(sDark.mAccent), 115), sDark.mWindow));
            EXPECT_EQ(at(sEnabled | QStyle::State_NoChange).pixel(8, 8), sDark.mOnAccent) << "the dash";
        }

        /// **A combo box that only picks wears a button's look, one that edits a field's**, with the
        /// chevron in the text's ink at the arrow's place.
        TEST_F(GraphiteStyleTest, aComboBoxIsAButtonOrAFieldWithAChevron)
        {
            const auto at = [&](QStyle::State state, bool editable) {
                QStyleOptionComboBox option = optionOf<QStyleOptionComboBox>(state);
                option.editable = editable;
                option.subControls = QStyle::SC_All;
                return rendered([&](QPainter& painter) {
                    mStyle.drawComplexControl(QStyle::CC_ComboBox, &option, &painter, nullptr);
                });
            };
            EXPECT_EQ(at(sEnabled, false).pixel(10, sHeight / 2), sDark.mElementMid);
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver, false).pixel(10, sHeight / 2), sDark.mElementStrong);
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver, true).pixel(10, sHeight / 2), sDark.mElementMid)
                << "a field does not answer the pointer";
            EXPECT_EQ(at(sEnabled | QStyle::State_HasFocus, true).pixel(sEdge), sDark.mBorderFocused);

            QStyleOptionComboBox option = optionOf<QStyleOptionComboBox>(sEnabled);
            const QRect arrow = mStyle.subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, nullptr);
            const QImage picked = at(sEnabled, false);
            // The chevron's tip, three pixels under the arrow's centre.
            EXPECT_NE(picked.pixel(arrow.center().x(), arrow.center().y() + 2), sDark.mElementMid);
        }

        /// **A spin box is a field whose arrows light under the pointer**, and whose arrow that cannot step
        /// is drawn in the disabled ink.
        TEST_F(GraphiteStyleTest, aSpinBoxIsAFieldWhoseArrowsLightUnderThePointer)
        {
            const auto at = [&](QStyle::State state, QStyle::SubControls active,
                                QAbstractSpinBox::StepEnabled steps = QAbstractSpinBox::StepUpEnabled) {
                QStyleOptionSpinBox option = optionOf<QStyleOptionSpinBox>(state);
                option.subControls = QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown | QStyle::SC_SpinBoxFrame;
                option.activeSubControls = active;
                option.stepEnabled = steps;
                return rendered([&](QPainter& painter) {
                    mStyle.drawComplexControl(QStyle::CC_SpinBox, &option, &painter, nullptr);
                });
            };
            QStyleOptionSpinBox option = optionOf<QStyleOptionSpinBox>(sEnabled);
            const QRect up = mStyle.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, nullptr);
            // A corner of the up arrow its upward chevron leaves free.
            const QPoint upCorner = up.topLeft() + QPoint(3, 3);

            EXPECT_EQ(at(sEnabled, QStyle::SC_None).pixel(10, sHeight / 2), sDark.mElementMid);
            EXPECT_EQ(at(sEnabled, QStyle::SC_None).pixel(upCorner), sDark.mElementMid);
            EXPECT_EQ(
                at(sEnabled | QStyle::State_MouseOver, QStyle::SC_SpinBoxUp).pixel(upCorner), sDark.mElementStrong);
            const QRect down = mStyle.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, nullptr);
            const QImage downward = at(sEnabled | QStyle::State_MouseOver, QStyle::SC_SpinBoxDown);
            EXPECT_EQ(downward.pixel(upCorner), sDark.mElementMid) << "the arrow the pointer is not on lit";
            // The corner a downward chevron leaves free, as the up arrow's top corner is.
            EXPECT_EQ(downward.pixel(down.bottomLeft() + QPoint(3, -3)), sDark.mElementMid)
                << "an arrow that cannot step lit";
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver, QStyle::SC_SpinBoxDown,
                          QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled)
                          .pixel(down.bottomLeft() + QPoint(3, -3)),
                sDark.mElementStrong);
        }

        /// **A tab is palantir's chip**: the selected one the window's ground with the accent's cap of two
        /// pixels at its foot, another nothing at rest and `#454545` under the pointer.
        TEST_F(GraphiteStyleTest, aSelectedTabIsTheWindowsGroundWithTheAccentsCap)
        {
            const auto at = [&](QStyle::State state) {
                QStyleOptionTab option = optionOf<QStyleOptionTab>(state);
                option.shape = QTabBar::RoundedNorth;
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawControl(QStyle::CE_TabBarTabShape, &option, &painter, nullptr);
                    },
                    sWidth, sHeight, sGround);
            };
            const QImage selected = at(sEnabled | QStyle::State_Selected);
            EXPECT_EQ(selected.pixel(sMiddle), sDark.mWindow);
            EXPECT_EQ(selected.pixel(sWidth / 2, sHeight - 1), sDark.mAccent);
            EXPECT_EQ(selected.pixel(sWidth / 2, sHeight - 2), sDark.mAccent);
            EXPECT_EQ(selected.pixel(sWidth / 2, sHeight - 3), sDark.mWindow);
            EXPECT_EQ(selected.pixel(0, 0), sGround) << "a corner the chip rounds";

            EXPECT_EQ(at(sEnabled).pixel(sMiddle), sGround);
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver).pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver).pixel(sWidth / 2, sHeight - 1), sDark.mElementMid);
        }

        /// **The panes and frames**: a tab widget's pane is the window's ground behind the soft edge; a
        /// view's frame and a group box's are the soft edge alone; a menu is `#383838` behind the middle
        /// edge.
        TEST_F(GraphiteStyleTest, thePanesAndFramesAreTheirGroundsBehindTheirEdges)
        {
            const auto at = [&](QStyle::PrimitiveElement element) {
                const QStyleOption option = optionOf<QStyleOption>(sEnabled);
                return rendered([&](QPainter& painter) { mStyle.drawPrimitive(element, &option, &painter, nullptr); },
                    sWidth, sHeight, sGround);
            };
            const QImage pane = at(QStyle::PE_FrameTabWidget);
            EXPECT_EQ(pane.pixel(sMiddle), sDark.mWindow);
            EXPECT_EQ(pane.pixel(sEdge), blended(borderSoft(sDark), sDark.mWindow));

            for (const QStyle::PrimitiveElement frame : { QStyle::PE_Frame, QStyle::PE_FrameGroupBox })
            {
                const QImage edged = at(frame);
                EXPECT_EQ(edged.pixel(sMiddle), sGround) << frame;
                EXPECT_EQ(edged.pixel(sEdge), blended(borderSoft(sDark), sGround)) << frame;
            }
            EXPECT_EQ(at(QStyle::PE_Frame).pixel(0, 0), blended(borderSoft(sDark), sGround)) << "a square corner";

            const QImage menu = at(QStyle::PE_PanelMenu);
            EXPECT_EQ(menu.pixel(sMiddle), sDark.mElement);
            EXPECT_EQ(menu.pixel(sEdge), blended(borderMid(sDark), sDark.mElement));
        }

        /// **A selected row is a fill and the accent's pill at its start**: `#454545`, `#525252` under the
        /// pointer, the pill 3 px across held 6 px off the row's ends and drawn by the row's first cell
        /// alone; a row under the pointer `#454545`, and nothing at rest.
        TEST_F(GraphiteStyleTest, aSelectedRowIsAFillAndTheAccentsPillAtItsStart)
        {
            const auto at = [&](QStyle::State state,
                                QStyleOptionViewItem::ViewItemPosition position = QStyleOptionViewItem::OnlyOne) {
                QStyleOptionViewItem option = optionOf<QStyleOptionViewItem>(state);
                option.viewItemPosition = position;
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, nullptr);
                    },
                    sWidth, sHeight, sGround);
            };
            const QImage selected = at(sEnabled | QStyle::State_Selected);
            EXPECT_EQ(selected.pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(selected.pixel(1, sHeight / 2), sDark.mAccent);
            EXPECT_EQ(selected.pixel(4, sHeight / 2), sDark.mElementMid) << "a pill wider than 3";
            EXPECT_EQ(selected.pixel(1, 2), sDark.mElementMid) << "a pill reaching the row's end";
            EXPECT_EQ(
                at(sEnabled | QStyle::State_Selected | QStyle::State_MouseOver).pixel(sMiddle), sDark.mElementStrong);
            EXPECT_EQ(at(sEnabled | QStyle::State_Selected, QStyleOptionViewItem::Middle).pixel(1, sHeight / 2),
                sDark.mElementMid)
                << "a pill in a row's middle cell";
            EXPECT_EQ(at(sEnabled | QStyle::State_MouseOver).pixel(sMiddle), sDark.mElementMid);
            EXPECT_EQ(at(sEnabled).pixel(sMiddle), sGround);

            QStyleOptionViewItem item = optionOf<QStyleOptionViewItem>(sEnabled);
            const QSize fusion
                = mStyle.baseStyle()->sizeFromContents(QStyle::CT_ItemViewItem, &item, QSize(40, 16), nullptr);
            EXPECT_EQ(
                mStyle.sizeFromContents(QStyle::CT_ItemViewItem, &item, QSize(40, 16), nullptr), fusion + QSize(0, 8))
                << "a row 4 px above and below";
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_HeaderMargin, nullptr, nullptr), 8);
        }

        /// **What a view's row shows starts 8 px in**, its check box where it has one and its text where it has
        /// none, clear of the selected row's pill: a view lays a row out a margin and one more in from its ends,
        /// and the margin is 7 for a view alone, where every other widget keeps Fusion's.
        TEST_F(GraphiteStyleTest, aViewsRowStartsAtTheFieldsInset)
        {
            QListView view;
            view.setStyle(&mStyle);
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, &view), 7);
            const QWidget other;
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, &other),
                mStyle.baseStyle()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, &other));

            QStyleOptionViewItem item = optionOf<QStyleOptionViewItem>(sEnabled, QRect(0, 0, 200, sHeight));
            item.widget = &view;
            item.text = QStringLiteral("Morrowind.esm");
            item.features = QStyleOptionViewItem::HasDisplay;

            item.features |= QStyleOptionViewItem::HasCheckIndicator;
            const QRect check = mStyle.subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &item, &view);
            EXPECT_EQ(check.left(), 8) << "the check box first";
            EXPECT_GT(mStyle.subElementRect(QStyle::SE_ItemViewItemText, &item, &view).left(), check.right())
                << "and the text after it";
        }

        /// **A progress bar is `#454545` filled with the accent as far as it came**: half of 80 pixels is
        /// the first 40, from the right where the bar runs right to left.
        TEST_F(GraphiteStyleTest, aProgressBarIsFilledWithTheAccentAsFarAsItCame)
        {
            const auto at = [&](bool inverted) {
                QStyleOptionProgressBar option = optionOf<QStyleOptionProgressBar>(sEnabled | QStyle::State_Horizontal);
                option.minimum = 0;
                option.maximum = 10;
                option.progress = 5;
                option.invertedAppearance = inverted;
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawControl(QStyle::CE_ProgressBarGroove, &option, &painter, nullptr);
                        mStyle.drawControl(QStyle::CE_ProgressBarContents, &option, &painter, nullptr);
                    },
                    sWidth, sHeight, sGround);
            };
            const QImage forward = at(false);
            EXPECT_EQ(forward.pixel(20, sHeight / 2), sDark.mAccent);
            EXPECT_EQ(forward.pixel(39, sHeight / 2), sDark.mAccent);
            EXPECT_EQ(forward.pixel(60, sHeight / 2), sDark.mElementMid);
            const QImage backward = at(true);
            EXPECT_EQ(backward.pixel(20, sHeight / 2), sDark.mElementMid);
            EXPECT_EQ(backward.pixel(60, sHeight / 2), sDark.mAccent);
        }

        /// **A scroll bar is palantir's pill with no arrows**: 12 pixels across, the pill 8 of them on the
        /// far side of a gap of 4. A page of 100 over a range of 100 is a thumb of 100 · 100 / 200 = 50,
        /// at the top at the start and at 50 at the end, the pages either side of it.
        TEST_F(GraphiteStyleTest, aScrollBarIsAPillWithNoArrows)
        {
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, nullptr), 12);
            QStyleOptionSlider option = optionOf<QStyleOptionSlider>(sEnabled, QRect(0, 0, 12, 100));
            option.orientation = Qt::Vertical;
            option.minimum = 0;
            option.maximum = 100;
            option.pageStep = 100;
            option.sliderPosition = 0;
            const auto rectOf = [&](QStyle::SubControl part) {
                return mStyle.subControlRect(QStyle::CC_ScrollBar, &option, part, nullptr);
            };
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarSlider), QRect(4, 0, 8, 50));
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarAddPage), QRect(4, 50, 8, 50));
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarSubPage), QRect(4, 0, 8, 0));
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarGroove), QRect(0, 0, 12, 100));
            EXPECT_TRUE(rectOf(QStyle::SC_ScrollBarAddLine).isEmpty());
            EXPECT_TRUE(rectOf(QStyle::SC_ScrollBarSubLine).isEmpty());
            option.sliderPosition = 100;
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarSlider), QRect(4, 50, 8, 50));

            option.orientation = Qt::Horizontal;
            option.rect = QRect(0, 0, 100, 12);
            option.sliderPosition = 0;
            EXPECT_EQ(rectOf(QStyle::SC_ScrollBarSlider), QRect(0, 4, 50, 8));

            const auto thumbAt = [&](QStyle::State state, QStyle::SubControls active) {
                QStyleOptionSlider drawn = option;
                drawn.state = state | QStyle::State_Horizontal;
                drawn.activeSubControls = active;
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawComplexControl(QStyle::CC_ScrollBar, &drawn, &painter, nullptr);
                    },
                    100, 12, sGround)
                    .pixel(25, 8);
            };
            const auto muted = [](int alpha) {
                return blended(
                    qRgba(qRed(sDark.mTextMuted), qGreen(sDark.mTextMuted), qBlue(sDark.mTextMuted), alpha), sGround);
            };
            EXPECT_EQ(thumbAt(sEnabled, QStyle::SC_None), muted(115));
            EXPECT_EQ(thumbAt(sEnabled | QStyle::State_MouseOver, QStyle::SC_ScrollBarSlider), muted(166));
            EXPECT_EQ(thumbAt(sEnabled | QStyle::State_Sunken, QStyle::SC_ScrollBarSlider), muted(217));
            EXPECT_EQ(thumbAt(sEnabled | QStyle::State_MouseOver, QStyle::SC_ScrollBarAddPage), muted(115));
            EXPECT_EQ(rendered(
                          [&](QPainter& painter) {
                              mStyle.drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, nullptr);
                          },
                          100, 12, sGround)
                          .pixel(75, 8),
                sGround)
                << "a track behind the thumb";
        }

        /// **A dialog's buttons are their words**, disabled text is never dithered or etched, and a mnemonic
        /// is underlined only while Alt is held.
        TEST_F(GraphiteStyleTest, aDialogsButtonsCarryNoIcons)
        {
            EXPECT_EQ(mStyle.styleHint(QStyle::SH_UnderlineShortcut, nullptr, nullptr, nullptr), 0);
            EXPECT_EQ(mStyle.styleHint(QStyle::SH_DialogButtonBox_ButtonsHaveIcons, nullptr, nullptr, nullptr), 0);
            EXPECT_EQ(mStyle.styleHint(QStyle::SH_DitherDisabledText, nullptr, nullptr, nullptr), 0);
            EXPECT_EQ(mStyle.styleHint(QStyle::SH_EtchDisabledText, nullptr, nullptr, nullptr), 0);
        }

        /// **A button and the fields beside it are one row's height**: the text, 6 px above and below and the
        /// hairline each side, 14 px over the font's height, and never under what Fusion asked.
        TEST_F(GraphiteStyleTest, aButtonAndTheFieldsBesideItAreOneRowsHeight)
        {
            QStyleOptionButton option = optionOf<QStyleOptionButton>(sEnabled);
            option.text = QStringLiteral("Launch");
            const int row = option.fontMetrics.height() + 14;
            for (const QStyle::ContentsType type :
                { QStyle::CT_PushButton, QStyle::CT_LineEdit, QStyle::CT_ComboBox, QStyle::CT_SpinBox })
            {
                const QSize contents(40, option.fontMetrics.height());
                EXPECT_EQ(mStyle.sizeFromContents(type, &option, contents, nullptr).height(), row) << type;
                if (type != QStyle::CT_SpinBox)
                    EXPECT_EQ(mStyle.sizeFromContents(type, &option, QSize(40, 3 * row), nullptr).height(),
                        mStyle.baseStyle()->sizeFromContents(type, &option, QSize(40, 3 * row), nullptr).height())
                        << type << ": a content taller than the row kept";
                else
                    EXPECT_EQ(mStyle.sizeFromContents(type, &option, QSize(40, row), nullptr).height(), row)
                        << "a spin box's line edit, already a row, taken past one by Fusion's frame";
            }
            // A push button pads its text 12 either side and the hairline, never narrower than Fusion's.
            EXPECT_EQ(mStyle.sizeFromContents(QStyle::CT_PushButton, &option, QSize(200, 10), nullptr).width(), 226);
            const int fusionShort
                = mStyle.baseStyle()->sizeFromContents(QStyle::CT_PushButton, &option, QSize(10, 10), nullptr).width();
            EXPECT_EQ(mStyle.sizeFromContents(QStyle::CT_PushButton, &option, QSize(10, 10), nullptr).width(),
                std::max(fusionShort, 36));

            for (const QStyle::ContentsType type : { QStyle::CT_CheckBox, QStyle::CT_RadioButton })
            {
                const QSize fusion = mStyle.baseStyle()->sizeFromContents(type, &option, QSize(40, 10), nullptr);
                EXPECT_EQ(mStyle.sizeFromContents(type, &option, QSize(40, 10), nullptr),
                    QSize(fusion.width(), option.fontMetrics.height() + 8))
                    << type << ": a toggle stands its text and 4 px above and below, as wide as Fusion's";
            }

            // A tool button that shows only its icon is a row's square; one with its text keeps Fusion's size.
            QStyleOptionToolButton tool = optionOf<QStyleOptionToolButton>(sEnabled);
            tool.toolButtonStyle = Qt::ToolButtonIconOnly;
            EXPECT_EQ(mStyle.sizeFromContents(QStyle::CT_ToolButton, &tool, QSize(16, 16), nullptr), QSize(row, row));
            const QSize wide
                = mStyle.baseStyle()->sizeFromContents(QStyle::CT_ToolButton, &tool, QSize(3 * row, 16), nullptr);
            EXPECT_EQ(mStyle.sizeFromContents(QStyle::CT_ToolButton, &tool, QSize(3 * row, 16), nullptr),
                QSize(wide.width(), wide.width()))
                << "an icon wider than a row, square at Fusion's width";
            tool.toolButtonStyle = Qt::ToolButtonTextUnderIcon;
            EXPECT_EQ(mStyle.sizeFromContents(QStyle::CT_ToolButton, &tool, QSize(40, 50), nullptr),
                mStyle.baseStyle()->sizeFromContents(QStyle::CT_ToolButton, &tool, QSize(40, 50), nullptr));
        }

        /// **A group box with no title draws no frame**, one with a title its soft edge: the frame's left side,
        /// half way down, is the ground under the one and the edge over it under the other.
        TEST_F(GraphiteStyleTest, aGroupBoxWithNoTitleDrawsNoFrame)
        {
            constexpr int tall = 60;
            const auto at = [&](const QString& title) {
                QStyleOptionGroupBox group = optionOf<QStyleOptionGroupBox>(sEnabled, QRect(0, 0, sWidth, tall));
                group.text = title;
                group.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
                return rendered(
                    [&](QPainter& painter) {
                        mStyle.drawComplexControl(QStyle::CC_GroupBox, &group, &painter, nullptr);
                    },
                    sWidth, tall, sGround);
            };
            EXPECT_EQ(at(QString()).pixel(0, tall / 2), sGround);
            EXPECT_EQ(at(QStringLiteral("Content List")).pixel(0, tall / 2), blended(borderSoft(sDark), sGround));

            // **And lays out with no margin of its own**, where one with a title, or one that can be checked, keeps
            // the layout's 12.
            QGroupBox bare;
            QGroupBox titled(QStringLiteral("Content List"));
            QGroupBox checkable;
            checkable.setCheckable(true);
            for (const QStyle::PixelMetric margin : { QStyle::PM_LayoutLeftMargin, QStyle::PM_LayoutTopMargin,
                     QStyle::PM_LayoutRightMargin, QStyle::PM_LayoutBottomMargin })
            {
                EXPECT_EQ(mStyle.pixelMetric(margin, nullptr, &bare), 0) << margin;
                EXPECT_EQ(mStyle.pixelMetric(margin, nullptr, &titled), 12) << margin;
                EXPECT_EQ(mStyle.pixelMetric(margin, nullptr, &checkable), 12) << margin;
                EXPECT_EQ(mStyle.pixelMetric(margin, nullptr, nullptr), 12) << margin;
            }
        }

        /// **A label held to the top of its cell stands its text on the line a field's does**, the row's 6 px and
        /// its hairline further down; a label in its cell's middle is left as it was.
        TEST_F(GraphiteStyleTest, aTopAlignedLabelStandsOnAFieldsTextLine)
        {
            QLabel top(QStringLiteral("Resolution"));
            top.setAlignment(Qt::AlignLeft | Qt::AlignTop);
            mStyle.polish(&top);
            EXPECT_EQ(top.contentsMargins(), QMargins(0, 7, 0, 0));

            QLabel middle(QStringLiteral("Screen"));
            mStyle.polish(&middle);
            EXPECT_EQ(middle.contentsMargins(), QMargins(0, 0, 0, 0));
        }

        /// **A field's text starts 8 px in**, the 2 px a line edit adds inside the rectangle included; a
        /// field with no frame lies in a spin box or a combo box, which placed it.
        TEST_F(GraphiteStyleTest, aFieldsTextStartsAtTheFieldsInset)
        {
            QStyleOptionFrame framed = optionOf<QStyleOptionFrame>(sEnabled);
            framed.lineWidth = 1;
            EXPECT_EQ(mStyle.subElementRect(QStyle::SE_LineEditContents, &framed, nullptr), QRect(6, 1, 68, 22));
            QStyleOptionFrame bare = framed;
            bare.lineWidth = 0;
            EXPECT_EQ(mStyle.subElementRect(QStyle::SE_LineEditContents, &bare, nullptr),
                mStyle.baseStyle()->subElementRect(QStyle::SE_LineEditContents, &bare, nullptr));

            // A combo box's label, which adds 1 px, and its editor, which adds 2, both at 8, up to the chevron's
            // column at the far end: the chevron's 10 with 8 either side, `80 - 26 = 54`.
            QStyleOptionComboBox combo = optionOf<QStyleOptionComboBox>(sEnabled);
            EXPECT_EQ(mStyle.subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxArrow, nullptr),
                QRect(54, 0, 26, sHeight));
            EXPECT_EQ(mStyle.subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxEditField, nullptr),
                QRect(7, 1, 54 - 7, 22));
            combo.editable = true;
            EXPECT_EQ(mStyle.subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxEditField, nullptr),
                QRect(6, 1, 54 - 6, 22));
            combo.direction = Qt::RightToLeft;
            EXPECT_EQ(mStyle.subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxArrow, nullptr),
                QRect(0, 0, 26, sHeight))
                << "the chevron at the far end, which is the left under a right-to-left layout";
        }

        /// **A spin box is a field and a column of 26 px**, a combo box's chevron column, its up arrow above its
        /// down, each half of a row; one that shows no arrows is the field alone.
        TEST_F(GraphiteStyleTest, aSpinBoxIsAFieldAndAColumnOfTwoArrows)
        {
            QStyleOptionSpinBox option = optionOf<QStyleOptionSpinBox>(sEnabled);
            const auto rectOf = [&](QStyle::SubControl part) {
                return mStyle.subControlRect(QStyle::CC_SpinBox, &option, part, nullptr);
            };
            EXPECT_EQ(rectOf(QStyle::SC_SpinBoxUp), QRect(54, 0, 26, 12));
            EXPECT_EQ(rectOf(QStyle::SC_SpinBoxDown), QRect(54, 12, 26, 12));
            EXPECT_EQ(rectOf(QStyle::SC_SpinBoxEditField), QRect(6, 1, 48, 22));
            EXPECT_EQ(rectOf(QStyle::SC_SpinBoxFrame), QRect(0, 0, sWidth, sHeight));
            option.buttonSymbols = QAbstractSpinBox::NoButtons;
            EXPECT_TRUE(rectOf(QStyle::SC_SpinBoxUp).isEmpty());
            EXPECT_EQ(rectOf(QStyle::SC_SpinBoxEditField), QRect(6, 1, 74, 22));
        }

        /// **A menu pads 4 px above and below, its separator the soft edge held 4 px off its sides**, and a
        /// combo box's list leaves the menu's panel showing.
        TEST_F(GraphiteStyleTest, aMenuPadsItsRowsAndItsSeparatorIsTheSoftEdge)
        {
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_MenuVMargin, nullptr, nullptr), 4);

            QStyleOptionMenuItem separator = optionOf<QStyleOptionMenuItem>(sEnabled);
            separator.menuItemType = QStyleOptionMenuItem::Separator;
            const QImage drawn = rendered(
                [&](QPainter& painter) { mStyle.drawControl(QStyle::CE_MenuItem, &separator, &painter, nullptr); },
                sWidth, sHeight, sGround);
            EXPECT_EQ(drawn.pixel(sWidth / 2, separator.rect.center().y()), blended(borderSoft(sDark), sGround));
            EXPECT_EQ(drawn.pixel(2, separator.rect.center().y()), sGround);
            EXPECT_EQ(drawn.pixel(sWidth / 2, separator.rect.center().y() + 1), sGround);

            QComboBox combo;
            mStyle.polish(&combo);
            EXPECT_FALSE(combo.view()->viewport()->autoFillBackground());
        }

        /// **A combo box and a spin box grow by what the inset and the column take from Fusion's field**,
        /// so the text Fusion sized them for still fits.
        TEST_F(GraphiteStyleTest, aComboAndASpinBoxGrowByWhatTheirInsetTakes)
        {
            QStyleOptionComboBox combo = optionOf<QStyleOptionComboBox>(sEnabled);
            QStyleOptionSpinBox spin = optionOf<QStyleOptionSpinBox>(sEnabled);
            const QSize contents(100, 16);
            const auto grown = [&](QStyle::ContentsType type, auto option, QStyle::ComplexControl control,
                                   QStyle::SubControl field) {
                const QSize fusion = mStyle.baseStyle()->sizeFromContents(type, &option, contents, nullptr);
                option.rect = QRect(QPoint(0, 0), fusion);
                const int taken = mStyle.baseStyle()->subControlRect(control, &option, field, nullptr).width()
                    - mStyle.subControlRect(control, &option, field, nullptr).width();
                EXPECT_GT(taken, 0) << type;
                EXPECT_EQ(mStyle.sizeFromContents(type, &option, contents, nullptr).width(), fusion.width() + taken)
                    << type;
            };
            grown(QStyle::CT_ComboBox, combo, QStyle::CC_ComboBox, QStyle::SC_ComboBoxEditField);
            grown(QStyle::CT_SpinBox, spin, QStyle::CC_SpinBox, QStyle::SC_SpinBoxEditField);
        }

        /// **Layouts sit on palantir's grid**, 12 px at an edge and 8 between widgets, and **a rule is the soft
        /// edge**, one pixel across its frame's middle, either way round.
        TEST_F(GraphiteStyleTest, layoutsSitOnTheGridAndARuleIsTheSoftEdge)
        {
            for (const QStyle::PixelMetric margin : { QStyle::PM_LayoutLeftMargin, QStyle::PM_LayoutTopMargin,
                     QStyle::PM_LayoutRightMargin, QStyle::PM_LayoutBottomMargin })
                EXPECT_EQ(mStyle.pixelMetric(margin, nullptr, nullptr), 12) << margin;
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, nullptr), 8);
            EXPECT_EQ(mStyle.pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, nullptr), 8);

            const auto ruled = [&](QFrame::Shape shape) {
                QStyleOptionFrame option = optionOf<QStyleOptionFrame>(sEnabled);
                option.frameShape = shape;
                return rendered(
                    [&](QPainter& painter) { mStyle.drawControl(QStyle::CE_ShapedFrame, &option, &painter, nullptr); },
                    sWidth, sHeight, sGround);
            };
            const QRgb rule = blended(borderSoft(sDark), sGround);
            const QImage across = ruled(QFrame::HLine);
            EXPECT_EQ(across.pixel(sWidth / 2, (sHeight - 1) / 2), rule);
            EXPECT_EQ(across.pixel(sWidth / 2, (sHeight - 1) / 2 + 1), sGround);
            const QImage down = ruled(QFrame::VLine);
            EXPECT_EQ(down.pixel((sWidth - 1) / 2, sHeight / 2), rule);
            EXPECT_EQ(down.pixel((sWidth - 1) / 2 + 1, sHeight / 2), sGround);
        }

        /// **A message box's icon is a Lucide glyph in the ink of what it says**: a question and a notice in
        /// the text's, a warning in the warning's, a failure in the error ink, and any of them disabled in the
        /// disabled ink; every solid pixel of one drawn at 48 px is that ink.
        TEST_F(GraphiteStyleTest, aMessageBoxsIconIsAGlyphInTheInkOfWhatItSays)
        {
            const auto inkOf = [&](QStyle::StandardPixmap icon, QIcon::Mode mode) {
                const QImage drawn = mStyle.standardIcon(icon, nullptr, nullptr)
                                         .pixmap(48, 48, mode)
                                         .toImage()
                                         .convertToFormat(QImage::Format_ARGB32);
                std::vector<QRgb> inks;
                for (int y = 0; y < drawn.height(); ++y)
                    for (int x = 0; x < drawn.width(); ++x)
                        if (qAlpha(drawn.pixel(x, y)) == 255
                            && std::find(inks.begin(), inks.end(), drawn.pixel(x, y)) == inks.end())
                            inks.push_back(drawn.pixel(x, y));
                return inks;
            };
            EXPECT_EQ(inkOf(QStyle::SP_MessageBoxQuestion, QIcon::Normal), std::vector<QRgb>{ sDark.mText });
            EXPECT_EQ(inkOf(QStyle::SP_MessageBoxInformation, QIcon::Normal), std::vector<QRgb>{ sDark.mText });
            EXPECT_EQ(inkOf(QStyle::SP_MessageBoxWarning, QIcon::Normal), std::vector<QRgb>{ sDark.mWarning });
            EXPECT_EQ(inkOf(QStyle::SP_MessageBoxCritical, QIcon::Normal), std::vector<QRgb>{ errorInk(sDark) });
            EXPECT_EQ(inkOf(QStyle::SP_MessageBoxWarning, QIcon::Disabled), std::vector<QRgb>{ sDark.mTextDisabled });
        }
    }
}
