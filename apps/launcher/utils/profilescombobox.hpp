#pragma once

#include <cstdint>

#include <QWidget>

#include <components/contentselector/view/combobox.hpp>

#include "lineedit.hpp"

class QString;

class ProfilesComboBox : public ContentSelectorView::ComboBox
{
    Q_OBJECT

public:
    class ComboBoxLineEdit : public LineEdit
    {
    public:
        explicit ComboBoxLineEdit(QWidget* parent = nullptr);
    };

public:
    explicit ProfilesComboBox(QWidget* parent = nullptr);
    void setEditEnabled(bool editable);
    void setCurrentProfile(std::int32_t index)
    {
        ComboBox::setCurrentIndex(index);
        mOldProfile = currentText();
    }

signals:
    void signalProfileTextChanged(const QString& item);
    void signalProfileChanged(const QString& previous, const QString& current);
    void signalProfileChanged(std::int32_t index);
    void profileRenamed(const QString& oldName, const QString& newName);

private slots:

    void slotEditingFinished();
    void slotIndexChangedByUser(std::int32_t index);
    void slotTextChanged(const QString& text);

private:
    QString mOldProfile;
};
