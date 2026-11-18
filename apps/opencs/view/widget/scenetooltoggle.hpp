#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "scenetool.hpp"

class QHBoxLayout;
class QRect;
class QObject;
class QPoint;
class QWidget;

namespace CSVWidget
{
    class SceneToolbar;
    class PushButton;

    ///< \brief Multi-Toggle tool
    class SceneToolToggle : public SceneTool
    {
        Q_OBJECT

        struct ButtonDesc
        {
            std::uint32_t mMask{};
            std::string mSmallIcon;
            QString mName;
            std::int32_t mIndex{};
        };

        std::string mEmptyIcon;
        QWidget* mPanel;
        QHBoxLayout* mLayout;
        std::map<PushButton*, ButtonDesc> mButtons; // widget, id
        std::int32_t mButtonSize;
        std::int32_t mIconSize;
        QString mToolTip;
        PushButton* mFirst;

        void adjustToolTip();

        void adjustIcon();

        QRect getIconBox(std::int32_t index) const;

    public:
        SceneToolToggle(SceneToolbar* parent, QString toolTip, std::string emptyIcon);

        void showPanel(const QPoint& position) override;

        /// \attention After the last button has been added, setSelection must be called at
        /// least once to finalise the layout.
        ///
        /// \note The layout algorithm can not handle more than 9 buttons. To prevent this An
        /// attempt to add more will result in an exception being thrown.
        /// The small icons will be sized at (x-4)/3 (where x is the main icon size).
        void addButton(const std::string& icon, std::uint32_t mask, const std::string& smallIcon, const QString& name,
            const QString& tooltip = "");

        std::uint32_t getSelectionMask() const;

        /// \param or'ed button masks. buttons that do not exist will be ignored.
        void setSelectionMask(std::uint32_t selection);

    signals:

        void selectionChanged();

    private slots:

        void selected();
    };
}
