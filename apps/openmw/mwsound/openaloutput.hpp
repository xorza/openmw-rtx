#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <components/vfs/pathutil.hpp>

#include "soundoutput.hpp"

namespace MWSound
{
    class SoundManager;
    class SoundBase;
    class Sound;
    class Stream;

    class OpenALOutput : public SoundOutput
    {
        ALCdevice* mDevice;
        ALCcontext* mContext;

        struct
        {
            bool EXT_EFX : 1;
            bool SOFT_HRTF : 1;
        } ALC = { false, false };
        struct
        {
            bool SOFT_source_spatialize : 1;
        } AL = { false };

        typedef std::deque<ALuint> IDDq;
        IDDq mFreeSources;

        typedef std::vector<Sound*> SoundVec;
        SoundVec mActiveSounds;
        typedef std::vector<Stream*> StreamVec;
        StreamVec mActiveStreams;

        osg::Vec3f mListenerPos;
        osg::Vec3f mListenerVel;
        Environment mListenerEnv;

        ALuint mWaterFilter;
        ALuint mWaterEffect;
        ALuint mDefaultEffect;
        ALuint mEffectSlot;

        struct StreamThread;
        std::unique_ptr<StreamThread> mStreamThread;

        std::string mDeviceName;
        std::vector<ALCint> mContextAttributes;
        std::mutex mReopenMutex;

        class DefaultDeviceThread;
        std::unique_ptr<DefaultDeviceThread> mDefaultDeviceThread;

        void initCommon2D(ALuint source, const osg::Vec3f& pos, ALfloat gain, ALfloat pitch, bool loop, bool useenv);
        void initCommon3D(ALuint source, const osg::Vec3f& pos, const osg::Vec3f& vel, ALfloat mindist, ALfloat maxdist,
            ALfloat gain, ALfloat pitch, bool loop, bool useenv);

        void updateCommon(ALuint source, const osg::Vec3f& pos, const osg::Vec3f& vel, ALfloat maxdist, ALfloat gain,
            ALfloat pitch, bool useenv);

        float getTimeScaledPitch(SoundBase* sound);

        // `noexcept`, as OpenAL's callback type is: nothing may unwind into the library's thread.
        static void eventCallback(ALenum eventType, ALuint object, ALuint param, ALsizei length, const ALchar* message,
            void* userParam) noexcept;

        void onDisconnect();

    public:
        OpenALOutput& operator=(const OpenALOutput& rhs) = delete;
        OpenALOutput(const OpenALOutput& rhs) = delete;
        std::vector<std::string> enumerate() override;
        bool init(const std::string& devname, const std::string& hrtfname, HrtfMode hrtfmode) override;
        void deinit() override;

        std::vector<std::string> enumerateHrtf() override;

        std::pair<Sound_Handle, std::size_t> loadSound(VFS::Path::NormalizedView fname) override;
        std::size_t unloadSound(Sound_Handle data) override;

        bool playSound(Sound* sound, Sound_Handle data, float offset) override;
        bool playSound3D(Sound* sound, Sound_Handle data, float offset) override;
        void finishSound(Sound* sound) override;
        bool isSoundPlaying(Sound* sound) override;
        void updateSound(Sound* sound) override;

        bool streamSound(DecoderPtr decoder, Stream* sound, bool getLoudnessData = false) override;
        bool streamSound3D(DecoderPtr decoder, Stream* sound, bool getLoudnessData) override;
        void finishStream(Stream* sound) override;
        double getStreamDelay(Stream* sound) override;
        float getStreamOffset(Stream* sound) override;
        float getStreamLoudness(Stream* sound) override;
        bool isStreamPlaying(Stream* sound) override;
        void updateStream(Stream* sound) override;

        void startUpdate() override;
        void finishUpdate() override;

        void updateListener(const osg::Vec3f& pos, const osg::Vec3f& atdir, const osg::Vec3f& updir,
            const osg::Vec3f& vel, Environment env) override;

        void pauseSounds(std::int32_t types) override;
        void resumeSounds(std::int32_t types) override;

        void pauseActiveDevice() override;
        void resumeActiveDevice() override;

        explicit OpenALOutput(SoundManager& mgr);
        ~OpenALOutput() override;
    };
}
