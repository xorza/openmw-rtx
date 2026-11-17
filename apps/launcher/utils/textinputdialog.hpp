#pragma once

#include <cstdint>

#include <QDialog>

#include "lineedit.hpp"

class QDialogButtonBox;

namespace Launcher
{
    class TextInputDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit TextInputDialog(const QString& title, const QString& text, QWidget* parent = nullptr);
        ~TextInputDialog() override = default;

        inline LineEdit* lineEdit() { return mLineEdit; }
        void setOkButtonEnabled(bool enabled);

        std::int32_t exec() override;

    private:
        QDialogButtonBox* mButtonBox;
        LineEdit* mLineEdit;
    };
}
