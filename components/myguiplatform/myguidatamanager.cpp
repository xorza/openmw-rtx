#include "myguidatamanager.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <MyGUI_DataFileStream.h>

#include <components/vfs/manager.hpp>

namespace
{
    class DataStream final : public MyGUI::DataStream
    {
    public:
        explicit DataStream(std::unique_ptr<std::istream>&& stream)
            : MyGUI::DataStream(stream.get())
            , mOwnedStream(std::move(stream))
        {
        }

    private:
        std::unique_ptr<std::istream> mOwnedStream;
    };

    /// Bytes the data manager holds, read in place.
    class HeldStream final : public MyGUI::IDataStream
    {
    public:
        explicit HeldStream(std::span<const char> bytes)
            : mBytes(bytes)
        {
        }

        bool eof() override { return mAt >= mBytes.size(); }
        std::size_t size() override { return mBytes.size(); }

        void readline(std::string& line, MyGUI::Char delimiter) override
        {
            const auto from = mBytes.begin() + static_cast<std::ptrdiff_t>(mAt);
            const auto end = std::find(from, mBytes.end(), static_cast<char>(delimiter));
            line.assign(from, end);
            mAt = static_cast<std::size_t>(end - mBytes.begin()) + (end == mBytes.end() ? 0 : 1);
        }

        std::size_t read(void* into, std::size_t count) override
        {
            const std::size_t taken = std::min(count, mBytes.size() - mAt);
            std::memcpy(into, mBytes.data() + mAt, taken);
            mAt += taken;
            return taken;
        }

    private:
        std::span<const char> mBytes;
        std::size_t mAt = 0;
    };
}

namespace MyGUIPlatform
{

    void DataManager::setResourcePath(VFS::Path::NormalizedView path)
    {
        mResourcePath = path;
    }

    VFS::Path::NormalizedView DataManager::getResourcePath() const
    {
        return mResourcePath;
    }

    DataManager::DataManager(VFS::Path::NormalizedView resourcePath, const VFS::Manager* vfs)
        : mResourcePath(resourcePath)
        , mVfs(vfs)
    {
    }

    void DataManager::hold(std::string name, std::vector<char> bytes)
    {
        [[maybe_unused]] const bool added = mHeld.emplace(std::move(name), std::move(bytes)).second;
        assert(added && "a name held twice");
    }

    MyGUI::IDataStream* DataManager::getData(const std::string& name) const
    {
        if (const auto held = mHeld.find(name); held != mHeld.end())
            return new HeldStream(held->second);

        VFS::Path::Normalized path(mResourcePath);
        path /= name;
        return new DataStream(mVfs->get(path));
    }

    void DataManager::freeData(MyGUI::IDataStream* data)
    {
        delete data;
    }

    bool DataManager::isDataExist(const std::string& name) const
    {
        if (mHeld.contains(name))
            return true;

        VFS::Path::Normalized path(mResourcePath);
        path /= name;
        return mVfs->exists(path);
    }

    const MyGUI::VectorString& DataManager::getDataListNames(const std::string& /*pattern*/) const
    {
        throw std::runtime_error("DataManager::getDataListNames is not implemented - VFS is used");
    }

    std::string DataManager::getDataPath(const std::string& name) const
    {
        VFS::Path::Normalized path(mResourcePath);
        path /= name;
        if (!mVfs->exists(path))
            return {};

        return path;
    }

}
