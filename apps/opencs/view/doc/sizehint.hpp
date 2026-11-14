#pragma once

#include <QSize>
#include <QWidget>

namespace CSVDoc
{
    class SizeHintWidget : public QWidget
    {
        QSize mSize;

    public:
        explicit SizeHintWidget(QWidget* parent = nullptr)
            : QWidget(parent)
        {
        }

        ~SizeHintWidget() override = default;

        QSize sizeHint() const override { return mSize; }
        void setSizeHint(const QSize& size) { mSize = size; }
    };
}
