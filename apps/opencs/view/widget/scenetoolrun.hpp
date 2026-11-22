#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "scenetool.hpp"

class QObject;
class QPoint;

namespace CSVWidget
{
    class SceneToolbar;
    class SceneToolListPanel;

    class SceneToolRun : public SceneTool
    {
        Q_OBJECT

        std::set<std::string> mProfiles;
        std::set<std::string>::iterator mSelected;
        QString mToolTip;
        SceneToolListPanel* mPanel;

    private:
        void adjustToolTips();

        void updateIcon();

        void updatePanel();

    public:
        SceneToolRun(
            SceneToolbar* parent, QString toolTip, const QString& icon, const std::vector<std::string>& profiles);

        void showPanel(const QPoint& position) override;

        void activate() override;

        /// \attention This function does not remove the profile from the profile selection
        /// panel.
        void removeProfile(const std::string& profile);

        /// \attention This function doe not add the profile to the profile selection
        /// panel. This only happens when the panel is re-opened.
        ///
        /// \note Adding profiles that are already listed is a no-op.
        void addProfile(const std::string& profile);

    private slots:

        void selectRow(std::int32_t row);

        void removeRow(std::int32_t row);

    signals:

        void runRequest(const std::string& profile);
    };
}
