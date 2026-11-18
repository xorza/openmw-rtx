#pragma once

#include <filesystem>
#include <string>

#include "archive.hpp"
#include "file.hpp"

namespace VFS
{

    class FileSystemArchiveFile : public File
    {
    public:
        explicit FileSystemArchiveFile(std::filesystem::path path);

        Files::IStreamPtr open() override;

        std::filesystem::file_time_type getLastModified() const override;

        std::string getStem() const override;

    private:
        std::filesystem::path mPath;
    };

    class FileSystemArchive : public Archive
    {
    public:
        explicit FileSystemArchive(std::filesystem::path path);

        void listResources(FileMap& out) override;

        bool contains(Path::NormalizedView file) const override;

        std::string getDescription() const override;

    private:
        std::map<VFS::Path::Normalized, FileSystemArchiveFile, std::less<>> mIndex;
        std::filesystem::path mPath;
    };

}
