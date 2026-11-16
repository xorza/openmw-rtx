#include "page.hpp"

#include <QGridLayout>
#include <QLabel>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>

#include "pagebase.hpp"

CSVPrefs::Page::Page(CSMPrefs::Category& category, QWidget* parent)
    : PageBase(category, parent)
{
    auto* widget = new QWidget(parent);
    mGrid = new QGridLayout(widget);

    for (auto& entry : category)
        addSetting(entry);

    setWidget(widget);
}

void CSVPrefs::Page::addSetting(CSMPrefs::Setting* setting)
{
    const CSMPrefs::SettingWidgets widgets = setting->makeWidgets(this);

    int next = mGrid->rowCount();

    if (widgets.mLabel != nullptr && widgets.mInput != nullptr)
    {
        mGrid->addWidget(widgets.mLabel, next, 0);
        mGrid->addWidget(widgets.mInput, next, 1);
    }
    else if (widgets.mInput != nullptr)
    {
        mGrid->addWidget(widgets.mInput, next, 0, 1, 2);
    }
}
