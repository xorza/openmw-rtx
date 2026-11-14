#pragma once

#include <QMessageBox>

namespace Launcher
{
    class TextSlotMsgBox : public QMessageBox
    {
        Q_OBJECT
    public slots:
        void setTextSlot(const QString& string);
    };
}
