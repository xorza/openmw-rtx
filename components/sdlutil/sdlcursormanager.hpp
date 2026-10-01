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
    class SDLCursorManager
    {
    public:
        SDLCursorManager();
        virtual ~SDLCursorManager();

        /// \brief sets whether to actively manage cursors or not
        virtual void setEnabled(bool enabled);

        /// \brief Tell the manager that the cursor has changed, giving the
        ///        name of the cursor we changed to ("arrow", "ibeam", etc)
        virtual void cursorChanged(std::string_view name);

        /// A cursor of `width` × `height` window pixels, hot at `hotspotX`, `hotspotY` in the same
        /// pixels, which SDL shows at the display's scale `displayScale`: an image of the size
        /// divided by it, which is what SDL scales up, and the whole size beside it, which SDL picks
        /// instead. Shown at once where it is the current cursor.
        void createCursor(std::string_view name, int rotDegrees, osg::Image* image, int hotspotX, int hotspotY,
            int width, int height, float displayScale);

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
    };
}

#endif
