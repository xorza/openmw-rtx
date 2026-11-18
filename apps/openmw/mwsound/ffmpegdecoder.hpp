#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>

// From version 54.56 binkaudio encoding format changed from S16 to FLTP. See:
// https://gitorious.org/ffmpeg/ffmpeg/commit/7bfd1766d1c18f07b0a2dd042418a874d49ea60d
// https://ffmpeg.zeranoe.com/forum/viewtopic.php?f=15&t=872
#include <libswresample/swresample.h>
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <string>
#include <utility>
#include <vector>

#include <components/files/istreamptr.hpp>

#include "sounddecoder.hpp"

namespace MWSound
{
    bool hasAudioExtension(VFS::Path::NormalizedView fname);

    class HeadCache;

    struct AVIOContextDeleter
    {
        void operator()(AVIOContext* ptr) const;
    };

    using AVIOContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

    struct AVFormatContextDeleter
    {
        void operator()(AVFormatContext* ptr) const;
    };

    using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

    struct AVCodecContextDeleter
    {
        void operator()(AVCodecContext* ptr) const;
    };

    using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

    struct AVFrameDeleter
    {
        void operator()(AVFrame* ptr) const;
    };

    using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

    class FFmpegDecoder final : public SoundDecoder
    {
        AVIOContextPtr mIoCtx;
        AVFormatContextPtr mFormatCtx;
        AVCodecContextPtr mCodecCtx;
        AVStream** mStream;

        AVPacket mPacket{};
        AVFramePtr mFrame;

        std::size_t mFrameSize;
        std::size_t mFramePos;

        double mNextPts;

        SwrContext* mSwr;
        enum AVSampleFormat mOutputSampleFormat;
        AVChannelLayout mOutputChannelLayout;
        std::uint8_t* mDataBuf;
        std::uint8_t** mFrameData;
        std::int32_t mDataBufLen;

        bool getNextPacket();

        // Opens a format context over mDataStream and selects a usable audio
        // stream. Returns false on failure, leaving the stream position
        // undefined; the caller rewinds before another attempt.
        bool openContext(const char* name, const AVInputFormat* fmt, bool limitProbe, AVIOContextPtr& ioCtx,
            AVFormatContextPtr& formatCtx, AVStream**& stream);

        Files::IStreamPtr mDataStream;

        // Embedded cover art shims. A leading id3v2 tag is hidden behind a
        // window starting at mStreamBase; FLAC PICTURE metadata headers are
        // served with their type byte relabelled to PADDING (mPatchedBytes,
        // absolute offset -> byte). Both make the demuxer seek over art it
        // would otherwise read in full (multi-MB per file in the wild).
        std::streamoff mStreamBase = 0;
        std::vector<std::pair<std::streamoff, char>> mPatchedBytes;

        // Sniffs mDataStream and fills the shim state, leaving the stream
        // positioned at mStreamBase.
        void applyArtShims();

        static std::int32_t readPacket(void* userData, std::uint8_t* buf, std::int32_t bufSize);
        static std::int32_t writePacket(void* userData, const std::uint8_t* buf, std::int32_t bufSize);
        static std::int64_t seek(void* userData, std::int64_t offset, std::int32_t whence);

        bool getAVAudioData();
        std::size_t readAVAudioData(void* data, std::size_t length);

        void open(VFS::Path::NormalizedView fname) override;
        void close() override;

        std::string getName() override;
        void getInfo(std::int32_t* samplerate, ChannelConfig* chans, SampleType* type) override;

        std::size_t read(char* buffer, std::size_t bytes) override;
        void readAll(std::vector<char>& output) override;
        std::size_t getSampleOffset() override;

    public:
        FFmpegDecoder& operator=(const FFmpegDecoder& rhs) = delete;
        FFmpegDecoder(const FFmpegDecoder& rhs) = delete;
        explicit FFmpegDecoder(const VFS::Manager* vfs, HeadCache* headCache, bool recordHead);

        ~FFmpegDecoder() override;

        friend class SoundManager;
        friend class WarmQueue;

    private:
        HeadCache* mHeadCache;
        bool mRecordHead;
    };
}
