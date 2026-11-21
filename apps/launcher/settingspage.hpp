#pragma once

#include <cstdint>

#include <QCompleter>
#include <QStringListModel>

#include <components/files/configurationmanager.hpp>

#include "ui_settingspage.h"

namespace Config
{
    class GameSettings;
}

namespace Launcher
{
    class SettingsPage : public QWidget, private Ui::SettingsPage
    {
        Q_OBJECT

    public:
        explicit SettingsPage(const Files::ConfigurationManager& configurationManager,
            Config::GameSettings& gameSettings, QWidget* parent = nullptr);

        bool loadSettings();
        void saveSettings();

    public slots:
        void slotLoadedCellsChanged(const QStringList& cellNames);

    private slots:
        void slotSkipMenuToggled(bool checked);
        void on_runScriptAfterStartupBrowseButton_clicked();
        void slotAnimSourcesToggled(bool checked);
        void slotObjectPagingToggled(bool checked);
        void slotControllerMenusToggled(bool checked);
        void slotOpenFile(QTreeWidgetItem* item);

    private:
        void populateLoadedConfigs();

        const Files::ConfigurationManager& mCfgMgr;

        Config::GameSettings& mGameSettings;
        QCompleter mCellNameCompleter;
        QStringListModel mCellNameCompleterModel;

        /**
         * Load the cells associated with the given content files for use in autocomplete
         * @param filePaths the file paths of the content files to be examined
         */
        void loadCellsForAutocomplete(const QStringList& cellNames);
    };
}
