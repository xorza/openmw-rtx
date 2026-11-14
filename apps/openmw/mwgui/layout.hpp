#pragma once

#include <string>
#include <string_view>

#include <MyGUI_Widget.h>

#include <components/debug/debuglog.hpp>

namespace MWGui
{
    /** The Layout class is an utility class used to load MyGUI layouts
        from xml files, and to manipulate member widgets.
     */
    class Layout
    {
    public:
        explicit Layout(std::string_view layout)
            : mMainWidget(nullptr)
        {
            initialise(layout);
            assert(mMainWidget);
        }

        virtual ~Layout()
        {
            try
            {
                shutdown();
            }
            catch (const MyGUI::Exception& e)
            {
                Log(Debug::Error) << "Error in the destructor: " << e.what();
            }
        }
        Layout(const Layout&) = delete;
        Layout& operator=(const Layout&) = delete;

        MyGUI::Widget* getWidget(std::string_view name) const;

        template <typename T>
        void getWidget(T*& widget, std::string_view name) const
        {
            MyGUI::Widget* w = getWidget(name);
            T* cast = w->castType<T>(false);
            if (!cast)
            {
                MYGUI_EXCEPT("Error cast : dest type = '" << T::getClassTypeName() << "' source name = '"
                                                          << w->getName() << "' source type = '" << w->getTypeName()
                                                          << "' in layout '" << mLayoutName << "'");
            }
            else
                widget = cast;
        }

    private:
        void initialise(std::string_view layout);

        void shutdown();

    public:
        void setCoord(int x, int y, int w, int h);

        virtual void setVisible(bool b);

        void setText(std::string_view name, std::string_view caption);

        // NOTE: this assume that mMainWidget is of type Window.
        void setTitle(std::string_view title);

        MyGUI::Widget* mMainWidget;

    protected:
        std::string mPrefix;
        std::string mLayoutName;
        MyGUI::VectorWidgetPtr mListWindowRoot;
    };
}
