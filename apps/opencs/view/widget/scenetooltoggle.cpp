#include "scenetooltoggle.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>

#include "pushbutton.hpp"
#include "scenetool.hpp"
#include "scenetoolbar.hpp"

void CSVWidget::SceneToolToggle::adjustToolTip()
{
    QString toolTip = mToolTip;

    toolTip += "<p>Currently enabled: ";

    bool first = true;

    for (const auto& [button, desc] : mButtons)
        if (button->isChecked())
        {
            if (!first)
                toolTip += ", ";
            else
                first = false;

            toolTip += desc.mName;
        }

    if (first)
        toolTip += "none";

    toolTip += "<p>(left click to alter selection)";

    setToolTip(toolTip);
}

void CSVWidget::SceneToolToggle::adjustIcon()
{
    std::uint32_t selection = getSelectionMask();
    if (!selection)
        setIcon(QIcon(QString::fromUtf8(mEmptyIcon.c_str())));
    else
    {
        QPixmap pixmap(48, 48);
        pixmap.fill(QColor(0, 0, 0, 0));

        {
            QPainter painter(&pixmap);

            for (const auto& [button, desc] : mButtons)
                if (button->isChecked())
                {
                    painter.drawImage(getIconBox(desc.mIndex), QImage(QString::fromUtf8(desc.mSmallIcon.c_str())));
                }
        }

        setIcon(pixmap);
    }
}

QRect CSVWidget::SceneToolToggle::getIconBox(std::int32_t index) const
{
    // layout for a 3x3 grid
    std::int32_t xMax = 3;
    std::int32_t yMax = 3;

    // icon size
    std::int32_t xBorder = 1;
    std::int32_t yBorder = 1;

    std::int32_t iconXSize = (mIconSize - xBorder * (xMax + 1)) / xMax;
    std::int32_t iconYSize = (mIconSize - yBorder * (yMax + 1)) / yMax;

    std::int32_t y = index / xMax;
    std::int32_t x = index % xMax;

    auto total = static_cast<std::int32_t>(mButtons.size());

    std::int32_t actualYIcons = total / xMax;

    if (total % xMax)
        ++actualYIcons;

    if (actualYIcons != yMax)
    {
        // space out icons vertically, if there aren't enough to populate all rows
        std::int32_t diff = yMax - actualYIcons;
        yBorder += (diff * (yBorder + iconXSize)) / (actualYIcons + 1);
    }

    if (y == actualYIcons - 1)
    {
        // generating the last row of icons
        std::int32_t actualXIcons = total % xMax;

        if (actualXIcons)
        {
            // space out icons horizontally, if there aren't enough to fill the last row
            std::int32_t diff = xMax - actualXIcons;

            xBorder += (diff * (xBorder + iconXSize)) / (actualXIcons + 1);
        }
    }

    return QRect((iconXSize + xBorder) * x + xBorder, (iconYSize + yBorder) * y + yBorder, iconXSize, iconYSize);
}

CSVWidget::SceneToolToggle::SceneToolToggle(SceneToolbar* parent, QString toolTip, std::string emptyIcon)
    : SceneTool(parent)
    , mEmptyIcon(std::move(emptyIcon))
    , mButtonSize(parent->getButtonSize())
    , mIconSize(parent->getIconSize())
    , mToolTip(std::move(toolTip))
    , mFirst(nullptr)
{
    mPanel = new QFrame(this, Qt::Popup);

    mLayout = new QHBoxLayout(mPanel);

    mLayout->setContentsMargins(QMargins(0, 0, 0, 0));

    mPanel->setLayout(mLayout);
}

void CSVWidget::SceneToolToggle::showPanel(const QPoint& position)
{
    mPanel->move(position);
    mPanel->show();

    if (mFirst)
        mFirst->setFocus(Qt::OtherFocusReason);
}

void CSVWidget::SceneToolToggle::addButton(const std::string& icon, std::uint32_t mask, const std::string& smallIcon,
    const QString& name, const QString& tooltip)
{
    if (mButtons.size() >= 9)
        throw std::runtime_error("Exceeded number of buttons in toggle type tool");

    auto* button = new PushButton(
        QIcon(QPixmap(icon.c_str())), PushButton::Type_Toggle, tooltip.isEmpty() ? name : tooltip, mPanel);

    button->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    button->setIconSize(QSize(mIconSize, mIconSize));
    button->setFixedSize(mButtonSize, mButtonSize);

    mLayout->addWidget(button);

    ButtonDesc desc;
    desc.mMask = mask;
    desc.mSmallIcon = smallIcon;
    desc.mName = name;
    desc.mIndex = static_cast<std::int32_t>(mButtons.size());

    mButtons.insert(std::make_pair(button, desc));

    connect(button, &PushButton::clicked, this, &SceneToolToggle::selected);

    if (mButtons.size() == 1)
        mFirst = button;
}

std::uint32_t CSVWidget::SceneToolToggle::getSelectionMask() const
{
    std::uint32_t selection = 0;

    for (const auto& button : mButtons)
        if (button.first->isChecked())
            selection |= button.second.mMask;

    return selection;
}

void CSVWidget::SceneToolToggle::setSelectionMask(std::uint32_t selection)
{
    for (auto& button : mButtons)
        button.first->setChecked(selection & button.second.mMask);

    adjustToolTip();
    adjustIcon();
}

void CSVWidget::SceneToolToggle::selected()
{
    auto iter = mButtons.find(dynamic_cast<PushButton*>(sender()));

    if (iter != mButtons.end())
    {
        if (!iter->first->hasKeepOpen())
            mPanel->hide();

        adjustToolTip();
        adjustIcon();

        emit selectionChanged();
    }
}
