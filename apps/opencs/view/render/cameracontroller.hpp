#pragma once

#include <cstdint>
#include <vector>

#include <QObject>
#include <osg/Vec3d>

namespace osg
{
    class Camera;
    class Group;
}

namespace CSMPrefs
{
    class Shortcut;
}

namespace CSVRender
{
    class CameraController : public QObject
    {
        Q_OBJECT

    public:
        static const osg::Vec3d WorldUp;

        static const osg::Vec3d LocalUp;
        static const osg::Vec3d LocalLeft;
        static const osg::Vec3d LocalForward;

        explicit CameraController(QObject* parent);
        ~CameraController() override = default;

        bool isActive() const;

        osg::Camera* getCamera() const;
        double getCameraSensitivity() const;
        bool getInverted() const;
        double getSecondaryMovementMultiplier() const;
        double getWheelMovementMultiplier() const;

        void setCamera(osg::Camera*);
        void setCameraSensitivity(double value);
        void setInverted(bool value);
        void setSecondaryMovementMultiplier(double value);
        void setWheelMovementMultiplier(double value);

        // moves the camera to an intelligent position
        void setup(osg::Group* root, std::uint32_t mask, const osg::Vec3d& up);

        virtual void handleMouseMoveEvent(std::int32_t x, std::int32_t y) = 0;
        virtual void handleMouseScrollEvent(std::int32_t x) = 0;

        virtual void update(double dt) = 0;

    protected:
        void addShortcut(CSMPrefs::Shortcut* shortcut);

    private:
        bool mActive, mInverted;
        double mCameraSensitivity;
        double mSecondaryMoveMult;
        double mWheelMoveMult;

        osg::Camera* mCamera;

        std::vector<CSMPrefs::Shortcut*> mShortcuts;
    };

    class FreeCameraController : public CameraController
    {
        Q_OBJECT

    public:
        explicit FreeCameraController(QWidget* widget);

        double getLinearSpeed() const;
        double getRotationalSpeed() const;
        double getSpeedMultiplier() const;

        void setLinearSpeed(double value);
        void setRotationalSpeed(double value);
        void setSpeedMultiplier(double value);

        void fixUpAxis(const osg::Vec3d& up);
        void unfixUpAxis();

        void handleMouseMoveEvent(std::int32_t x, std::int32_t y) override;
        void handleMouseScrollEvent(std::int32_t x) override;

        void update(double dt) override;

    private:
        void yaw(double value);
        void pitch(double value);
        void roll(double value);
        void translate(const osg::Vec3d& offset);

        void stabilize();

        bool mLockUpright, mModified;
        bool mNaviPrimary, mNaviSecondary;
        bool mFast, mFastAlternate;
        bool mLeft, mRight, mForward, mBackward, mRollLeft, mRollRight;
        osg::Vec3d mUp;

        double mLinSpeed;
        double mRotSpeed;
        double mSpeedMult;

    private slots:

        void naviPrimary(bool active);
        void naviSecondary(bool active);
        void forward(bool active);
        void left(bool active);
        void backward(bool active);
        void right(bool active);
        void rollLeft(bool active);
        void rollRight(bool active);
        void alternateFast(bool active);
        void swapSpeedMode();
    };

    class OrbitCameraController : public CameraController
    {
        Q_OBJECT

    public:
        explicit OrbitCameraController(QWidget* widget);

        osg::Vec3d getCenter() const;
        double getOrbitSpeed() const;
        double getOrbitSpeedMultiplier() const;
        std::uint32_t getPickingMask() const;

        void setCenter(const osg::Vec3d& value);
        void setOrbitSpeed(double value);
        void setOrbitSpeedMultiplier(double value);
        void setPickingMask(std::uint32_t value);

        void handleMouseMoveEvent(std::int32_t x, std::int32_t y) override;
        void handleMouseScrollEvent(std::int32_t x) override;

        void update(double dt) override;

        /// \brief Flag controller to be re-initialized.
        void reset();

        void setConstRoll(bool enable);

    private:
        void initialize();

        void rotateHorizontal(double value);
        void rotateVertical(double value);
        void roll(double value);
        void translate(const osg::Vec3d& offset);
        void zoom(double value);

        bool mInitialized;
        bool mNaviPrimary, mNaviSecondary;
        bool mFast, mFastAlternate;
        bool mLeft, mRight, mUp, mDown, mRollLeft, mRollRight;
        std::uint32_t mPickingMask;
        osg::Vec3d mCenter;
        double mDistance;

        double mOrbitSpeed;
        double mOrbitSpeedMult;

        bool mConstRoll;

    private slots:

        void naviPrimary(bool active);
        void naviSecondary(bool active);
        void up(bool active);
        void left(bool active);
        void down(bool active);
        void right(bool active);
        void rollLeft(bool active);
        void rollRight(bool active);
        void alternateFast(bool active);
        void swapSpeedMode();
    };
}
