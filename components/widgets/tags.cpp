#include "tags.hpp"

#include <cstdint>

#include <MyGUI_Colour.h>

#include <components/fallback/fallback.hpp>

namespace Gui
{

    bool replaceTag(std::string_view tag, MyGUI::UString& out)
    {
        std::string_view fontcolour = "fontcolour=";

        std::string_view fontcolourhtml = "fontcolourhtml=";

        if (tag.starts_with(fontcolour))
        {
            std::string fallbackName = "FontColor_color_";
            fallbackName += tag.substr(fontcolour.length());
            std::string_view str = Fallback::Map::getString(fallbackName);
            if (str.empty())
                throw std::runtime_error("Unknown fallback name: " + fallbackName);

            std::string ret[3];
            std::uint32_t j = 0;
            for (std::uint32_t i = 0; i < str.length(); ++i)
            {
                if (str[i] == ',')
                    j++;
                else if (str[i] != ' ')
                    ret[j] += str[i];
            }
            MyGUI::Colour col(static_cast<float>(MyGUI::utility::parseInt(ret[0])) / 255.f,
                static_cast<float>(MyGUI::utility::parseInt(ret[1])) / 255.f,
                static_cast<float>(MyGUI::utility::parseInt(ret[2])) / 255.f);
            out = col.print();
            return true;
        }
        else if (tag.starts_with(fontcolourhtml))
        {
            std::string fallbackName = "FontColor_color_";
            fallbackName += tag.substr(fontcolourhtml.length());
            std::string_view str = Fallback::Map::getString(fallbackName);
            if (str.empty())
                throw std::runtime_error("Unknown fallback name: " + fallbackName);

            std::string ret[3];
            std::uint32_t j = 0;
            for (std::uint32_t i = 0; i < str.length(); ++i)
            {
                if (str[i] == ',')
                    j++;
                else if (str[i] != ' ')
                    ret[j] += str[i];
            }
            std::stringstream html;
            html << "#" << std::hex << MyGUI::utility::parseInt(ret[0]) << MyGUI::utility::parseInt(ret[1])
                 << MyGUI::utility::parseInt(ret[2]);
            out = html.str();
            return true;
        }
        return false;
    }

}
