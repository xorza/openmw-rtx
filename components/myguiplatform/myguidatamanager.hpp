#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_MYGUIDATAMANAGER_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_MYGUIDATAMANAGER_H

#include <MyGUI_DataManager.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <components/vfs/pathutil.hpp>

namespace VFS
{
    class Manager;
}

namespace MyGUIPlatform
{

    class DataManager : public MyGUI::DataManager
    {
    public:
        explicit DataManager(VFS::Path::NormalizedView path, const VFS::Manager* vfs);

        void setResourcePath(VFS::Path::NormalizedView path);
        VFS::Path::NormalizedView getResourcePath() const;

        /// Keeps `bytes` and answers `name` with them from now on, as it is spelt and whatever the
        /// resource path: what a font's source is read from each time it is rasterized, so a
        /// rasterization after the load reads nothing from the VFS and depends on no path set
        /// around it. A name held twice is a logic error.
        void hold(std::string name, std::vector<char> bytes);

        bool holds(std::string_view name) const { return mHeld.contains(name); }

        /** Get data stream from specified resource name.
            @param name Resource name (usually file name).
        */
        MyGUI::IDataStream* getData(const std::string& name) const override;

        /** Free data stream.
            @param data Data stream.
        */
        void freeData(MyGUI::IDataStream* data) override;

        /** Is data with specified name exist.
            @param name Resource name.
        */
        bool isDataExist(const std::string& name) const override;

        /** Get all data names with names that matches pattern.
            @param pattern Pattern to match (for example "*.layout").
        */
        const MyGUI::VectorString& getDataListNames(const std::string& pattern) const override;

        /** Get full path to data.
            @param name Resource name.
            @return Return full path to specified data.
        */
        std::string getDataPath(const std::string& name) const override;

    private:
        VFS::Path::Normalized mResourcePath;

        const VFS::Manager* mVfs;

        std::map<std::string, std::vector<char>, std::less<>> mHeld;
    };

}

#endif
