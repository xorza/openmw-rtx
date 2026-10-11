#include "rendersupport.hpp"

namespace MWRender
{
    std::string notAvailable(std::string_view what, std::string_view declined)
    {
        std::string answer(what);
        answer += " -> not available under this renderer: ";
        answer += declined;
        return answer;
    }

    const SettingSupport* RenderSupport::find(std::string_view category, std::string_view name) const
    {
        const SettingSupport* wholeCategory = nullptr;
        for (const SettingSupport& entry : mSettings)
        {
            if (entry.mCategory != category)
                continue;
            if (entry.mName == name)
                return &entry;
            if (entry.mName.empty())
                wholeCategory = &entry;
        }

        return wholeCategory;
    }

    std::string_view RenderSupport::declinedSetting(std::string_view category, std::string_view name) const
    {
        const SettingSupport* const entry = find(category, name);
        return entry != nullptr ? entry->mDeclined : std::string_view();
    }

    bool RenderSupport::namesSetting(std::string_view category, std::string_view name) const
    {
        return find(category, name) != nullptr;
    }

    std::string_view RenderSupport::declinedMode(RenderMode mode) const
    {
        for (const ModeSupport& entry : mModes)
            if (entry.mMode == mode)
                return entry.mDeclined;

        return {};
    }

    std::string_view RenderSupport::declinedRequest(PictureRequest request) const
    {
        for (const RequestSupport& entry : mRequests)
            if (entry.mRequest == request)
                return entry.mDeclined;

        return {};
    }
}
