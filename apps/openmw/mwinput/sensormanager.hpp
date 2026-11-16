#pragma once

#include <array>

#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_video.h>
#include <osg/Matrixf>
#include <osg/Vec3f>

#include <components/sdlutil/events.hpp>
#include <components/settings/settings.hpp>

namespace SDLUtil
{
    class InputWrapper;
}

namespace MWWorld
{
    class Player;
}

namespace MWInput
{
    class SensorManager : public SDLUtil::SensorListener
    {
    public:
        explicit SensorManager(SDL_Window* window);

        ~SensorManager() override;
        SensorManager(const SensorManager&) = delete;
        SensorManager& operator=(const SensorManager&) = delete;

        void init();

        void update(float dt);

        void sensorUpdated(const SDL_SensorEvent& arg) override;
        void displayOrientationChanged() override;
        void processChangedSettings(const Settings::CategorySettingVector& changed);

        bool isGyroAvailable() const;
        std::array<float, 3> getGyroValues() const;

    private:
        void updateSensors();
        void correctGyroscopeAxes();

        osg::Matrixf mRotation;
        osg::Vec3f mGyroValues;
        float mGyroUpdateTimer;

        SDL_Sensor* mGyroscope;
        SDL_Window* mWindow;
    };
}
