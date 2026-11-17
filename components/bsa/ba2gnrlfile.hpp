#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "bsafile.hpp"

namespace Bsa
{
    class BA2GNRLFile : private BSAFile
    {
    private:
        struct FileRecord
        {
            FileRecord();
            std::uint32_t mSize = 0;
            std::uint64_t mOffset;
            std::uint32_t mPackedSize = 0;
            bool isValid() const;
        };

        std::uint32_t mVersion{ 0u };

        using FolderRecord = std::map<std::pair<std::uint32_t, std::uint32_t>, FileRecord>;
        std::map<std::uint32_t, FolderRecord> mFolders;

        std::list<std::vector<char>> mFileNames;

        FileRecord getFileRecord(std::string_view str) const;

        Files::IStreamPtr getFile(const FileRecord& fileRecord);

        void loadFiles(std::uint32_t fileCount, std::istream& in);

    public:
        using BSAFile::getFilename;
        using BSAFile::getList;
        using BSAFile::getPath;
        using BSAFile::open;

        BA2GNRLFile();
        ~BA2GNRLFile() override;

        /// Read header information from the input source
        void readHeader(std::istream& input) override;

        Files::IStreamPtr getFile(const FileStruct* fileStruct);
        void addFile(const std::string& filename, std::istream& file);
    };
}
