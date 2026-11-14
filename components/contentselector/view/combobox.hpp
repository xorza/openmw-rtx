#ifndef COMBOBOX_HPP
#define COMBOBOX_HPP

#include <QComboBox>

class QString;
class QRegularExpressionValidator;

namespace ContentSelectorView
{
    class ComboBox : public QComboBox
    {
        Q_OBJECT

    public:
        explicit ComboBox(QWidget* parent = nullptr);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void setPlaceholderText(const QString& text);

    private:
        QString mPlaceholderText;

    protected:
        void paintEvent(QPaintEvent*) override;
        QRegularExpressionValidator* mValidator;
    };
}

#endif // COMBOBOX_HPP
