#ifndef OPENMW_APPS_RTXTOOL_WINDOW_H
#define OPENMW_APPS_RTXTOOL_WINDOW_H

#include <cstdint>
#include <string>
#include <vector>

#include <osg/Vec3f>

#include <vulkan/vulkan_core.h>

struct SDL_Window;

namespace RtxTool
{
    /// A camera someone steers with a mouse and a keyboard.
    ///
    /// Yaw turns about the world's up axis, which is +Z here, and pitch is clamped just short of
    /// vertical: straight up has no defined roll and the basis would collapse.
    class FlyCamera
    {
    public:
        /// Points the camera from `origin` at `target`, recovering the angles from the direction.
        void look(const osg::Vec3f& origin, const osg::Vec3f& target);

        /// Applies the keyboard state for a frame of `seconds`.
        void advance(float seconds);

        void turn(float deltaYaw, float deltaPitch);

        /// Multiplies the movement speed, for the mouse wheel.
        void scaleSpeed(float factor);

        const osg::Vec3f& getOrigin() const { return mOrigin; }
        osg::Vec3f getForward() const;
        osg::Vec3f getTarget() const { return mOrigin + getForward(); }
        float getSpeed() const { return mSpeed; }

    private:
        osg::Vec3f mOrigin;
        float mYaw = 0.0f;
        float mPitch = 0.0f;

        /// Units a second. Morrowind's player walks at about a hundred, so this is a fast walk and
        /// shift makes it a flight.
        float mSpeed = 900.0f;
    };

    /// An SDL window with a Vulkan surface on it.
    ///
    /// Created before the instance, because the instance has to be told which surface extensions
    /// this window needs and only the window knows.
    class Window
    {
    public:
        Window(const std::string& title, std::uint32_t width, std::uint32_t height);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        /// Instance extensions the surface will need.
        std::vector<const char*> getInstanceExtensions() const;

        /// The drawable size in pixels, which is not the window size on a scaled display.
        VkExtent2D getExtent() const;

        SDL_Window* getHandle() const { return mHandle; }

    private:
        SDL_Window* mHandle = nullptr;
    };

    /// The `VkSurfaceKHR` for a window.
    ///
    /// Its own type, and not a member of `Window`, for one reason: a surface must be destroyed
    /// before the instance that made it, and the window has to exist before the instance so the
    /// instance can be told which extensions the window wants. Declaring window, then instance, then
    /// surface makes the destruction order come out right on its own.
    class Surface
    {
    public:
        Surface(VkInstance instance, const Window& window);
        ~Surface();

        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;

        VkSurfaceKHR getHandle() const { return mHandle; }

    private:
        VkInstance mInstance = VK_NULL_HANDLE;
        VkSurfaceKHR mHandle = VK_NULL_HANDLE;
    };
}

#endif
