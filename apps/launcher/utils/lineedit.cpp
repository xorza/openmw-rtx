#include "lineedit.hpp"

#include <cstdint>

LineEdit::LineEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setupClearButton();
}

void LineEdit::setupClearButton()
{
    mClearButton = new QToolButton(this);
    mClearButton->setIcon(QIcon::fromTheme("edit-clear"));
    mClearButton->setCursor(Qt::ArrowCursor);
    mClearButton->setAutoRaise(true);
    mClearButton->hide();
    connect(mClearButton, &QToolButton::clicked, this, &LineEdit::clear);
    connect(this, &LineEdit::textChanged, this, &LineEdit::updateClearButton);
}

void LineEdit::resizeEvent(QResizeEvent*)
{
    QSize sz = mClearButton->sizeHint();
    std::int32_t frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    mClearButton->move(rect().right() - frameWidth - sz.width(), (rect().bottom() + 1 - sz.height()) / 2);
}

void LineEdit::updateClearButton(const QString& text)
{
    mClearButton->setVisible(!text.isEmpty());
}
