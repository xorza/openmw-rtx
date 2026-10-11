#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLCURSORMANAGER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLCURSORMANAGER_H

#include <map>
#include <string>
#include <string_view>

struct SDL_Cursor;

namespace osg
{
    class Image;
}

namespace SDLUtil
{
    /// How a video driver shows a cursor's image: at the window's points, scaled to the display
    /// from an image that size (`SDL_HINT_MOUSE_DPI_SCALE_CURSORS` on Windows, the buffer scale on
    /// Wayland, the backing scale on macOS), or as its pixels are, which X11 does.
    enum class CursorScaling
    {
        ByDisplay,
        AsPixels,
    };

    class SDLCursorManager
    {
    public:
        /// How the driver SDL names `driver` (`SDL_GetCurrentVideoDriver`) shows a cursor.
        static CursorScaling scalingOf(std::string_view driver);

        /// What a cursor's size in window pixels is divided by for the image SDL is handed: the
        /// display's scale where the driver scales it back up, and one where it shows its pixels.
        static float baseDivisor(CursorScaling scaling, float displayScale);

        SDLCursorManager();
        virtual ~SDLCursorManager();

        /// \brief sets whether to actively manage cursors or not
        virtual void setEnabled(bool enabled);

        /// \brief Tell the manager that the cursor has changed, giving the
        ///        name of the cursor we changed to ("arrow", "ibeam", etc)
        virtual void cursorChanged(std::string_view name);

        /// A cursor of `width` × `height` window pixels, hot at `hotspotX`, `hotspotY` in the same
        /// pixels, which SDL shows at the display's scale `displayScale`: an image of the size
        /// divided by `baseDivisor`, which is what SDL scales up, and the whole size beside it, which
        /// SDL picks instead. Shown at once where it is the current cursor.
        void createCursor(std::string_view name, double rotDegrees, osg::Image* image, int hotspotX, int hotspotY,
            int width, int height, float displayScale);

        /// The cursor `name` destroyed, where there is one: SDL shows its default until the next
        /// `cursorChanged`.
        void removeCursor(std::string_view name);

        /// Every cursor destroyed, for `createCursor` to make anew at another size. The current
        /// cursor's name is kept.
        void dropCursors();

    private:
        void _setGUICursor(std::string_view name);

        typedef std::map<std::string, SDL_Cursor*, std::less<>> CursorMap;
        CursorMap mCursorMap;

        std::string mCurrentCursor;
        bool mEnabled;
        bool mInitialized;
        CursorScaling mScaling;
    };
}

#endif
