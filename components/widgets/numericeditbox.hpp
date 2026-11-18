#pragma once

#include <cstdint>

#include <MyGUI_EditBox.h>

namespace Gui
{

    /**
     * @brief A variant of the EditBox that only allows integer inputs
     */
    class NumericEditBox final : public MyGUI::EditBox
    {
        MYGUI_RTTI_DERIVED(NumericEditBox)

    public:
        NumericEditBox()
            : mValue(0)
            , mMinValue(std::numeric_limits<std::int32_t>::min())
            , mMaxValue(std::numeric_limits<std::int32_t>::max())
        {
        }

        void initialiseOverride() override;
        void shutdownOverride() override;

        typedef MyGUI::delegates::MultiDelegate<std::int32_t> EventHandle_ValueChanged;
        EventHandle_ValueChanged eventValueChanged;

        /// @note Does not trigger eventValueChanged
        void setValue(std::int32_t value);
        std::int32_t getValue();

        void setMinValue(std::int32_t minValue);
        void setMaxValue(std::int32_t maxValue);

    private:
        void onEditTextChange(MyGUI::EditBox* sender);
        void onKeyLostFocus(MyGUI::Widget* newWidget) override;
        void onKeyButtonPressed(MyGUI::KeyCode key, MyGUI::Char character) override;

        std::int32_t mValue;

        std::int32_t mMinValue;
        std::int32_t mMaxValue;
    };

}
