#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bsafile.hpp"

namespace Bsa
{
    class BA2DX10File : private BSAFile
    {
    private:
        struct TextureChunkRecord
        {
            std::uint32_t mSize = 0;
            std::uint32_t mPackedSize = 0;
            std::int64_t mOffset = 0;
            std::uint16_t mStartMip = 0;
            std::uint16_t mEndMip = 0;
        };

        struct FileRecord
        {
            std::uint8_t mUnknownTex = 0;
            std::uint16_t mHeight = 0;
            std::uint16_t mWidth = 0;
            std::uint8_t mNumMips = 0;
            std::uint8_t mDXGIFormat = 0;
            std::uint16_t mCubeMaps = 0;
            std::vector<TextureChunkRecord> mTextureChunks;
        };

        std::uint32_t mVersion{ 0u };

        using FolderRecord = std::map<std::pair<std::uint32_t, std::uint32_t>, FileRecord>;
        std::map<std::uint32_t, FolderRecord> mFolders;

        std::list<std::vector<char>> mFileNames;

        std::optional<FileRecord> getFileRecord(std::string_view str) const;

        Files::IStreamPtr getFile(const FileRecord& fileRecord);

        void loadFiles(std::uint32_t fileCount, std::istream& in);

    public:
        using BSAFile::getFilename;
        using BSAFile::getList;
        using BSAFile::getPath;
        using BSAFile::open;

        BA2DX10File();
        ~BA2DX10File() override;

        /// Read header information from the input source
        void readHeader(std::istream& input) override;

        Files::IStreamPtr getFile(const FileStruct* fileStruct);
        void addFile(const std::string& filename, std::istream& file);
    };
}
