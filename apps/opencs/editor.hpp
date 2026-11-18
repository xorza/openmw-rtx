#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <QLockFile>
#include <QObject>
#include <QString>
#include <boost/program_options/variables_map.hpp>

#include <apps/opencs/model/doc/documentmanager.hpp>
#include <apps/opencs/model/prefs/state.hpp>
#include <apps/opencs/view/doc/filedialog.hpp>
#include <apps/opencs/view/doc/newgame.hpp>
#include <apps/opencs/view/doc/startup.hpp>
#include <apps/opencs/view/prefs/dialogue.hpp>
#include <apps/opencs/view/tools/merge.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/multidircollection.hpp>

class QLocalServer;
class QLocalSocket;

namespace CSMDoc
{
    class Document;
}

namespace CSVDoc
{
    class ViewManager;
}

namespace CS
{
    class Editor : public QObject
    {
        Q_OBJECT

        Files::ConfigurationManager mCfgMgr;
        boost::program_options::variables_map mConfigVariables;
        CSMPrefs::State mSettingsState;
        CSMDoc::DocumentManager mDocumentManager;
        CSVDoc::StartupDialogue mStartup;
        CSVDoc::NewGameDialogue mNewGame;
        CSVPrefs::Dialogue mSettings;
        CSVDoc::FileDialog mFileDialog;
        std::filesystem::path mLocal;
        std::filesystem::path mResources;
        std::filesystem::path mPid;
        QLockFile mLockFile;
        std::ofstream mPidFile;
        CSVTools::Merge mMerge;
        CSVDoc::ViewManager* mViewManager;
        std::filesystem::path mFileToLoad;
        Files::PathContainer mDataDirs;
        std::string mEncodingName;

        boost::program_options::variables_map readConfiguration();
        ///< Calls mCfgMgr.readConfiguration; should be used before initialization of mSettingsState as it depends on
        ///< the configuration.
        std::pair<Files::PathContainer, std::vector<std::string>> readConfig(bool quiet = false);
        ///< \return data paths

        // not implemented
        Editor(const Editor&);
        Editor& operator=(const Editor&);

    public:
        Editor(std::int32_t argc, char** argv);
        ~Editor() override;

        bool makeIPCServer();
        void connectToIPCServer();

        std::int32_t run();
        ///< \return error status

    private slots:

        void createGame();
        void createAddon();
        void cancelCreateGame();
        void cancelFileDialog();

        void loadDocument();
        void openFiles(
            const std::filesystem::path& path, const std::vector<std::filesystem::path>& discoveredFiles = {});
        void createNewFile(const std::filesystem::path& path);
        void createNewGame(const std::filesystem::path& file);

        void showStartup();

        void showSettings();

        void documentAdded(CSMDoc::Document* document);

        void documentAboutToBeRemoved(CSMDoc::Document* document);

        void lastDocumentDeleted();

        void mergeDocument(CSMDoc::Document* document);

    private:
        QString mIpcServerName;
        QLocalServer* mServer;
        QLocalSocket* mClientSocket;
    };
}
