
#include "numericeditbox.hpp"

#include <cstdint>

#include <components/misc/strings/conversion.hpp>

namespace Gui
{

    void NumericEditBox::initialiseOverride()
    {
        Base::initialiseOverride();
        eventEditTextChange += MyGUI::newDelegate(this, &NumericEditBox::onEditTextChange);

        mValue = 0;
        setCaption("0");
    }

    void NumericEditBox::shutdownOverride()
    {
        Base::shutdownOverride();
        eventEditTextChange -= MyGUI::newDelegate(this, &NumericEditBox::onEditTextChange);
    }

    void NumericEditBox::onEditTextChange(MyGUI::EditBox* sender)
    {
        const std::string newCaption = sender->getCaption();

        if (newCaption.empty())
        {
            return;
        }

        if (const auto conversion = Misc::StringUtils::toNumeric<std::int32_t>(newCaption))
        {
            mValue = conversion.value();

            const std::int32_t capped = std::clamp(mValue, mMinValue, mMaxValue);
            if (capped != mValue)
            {
                mValue = capped;
                setCaption(MyGUI::utility::toString(mValue));
            }
        }
        else
        {
            setCaption(MyGUI::utility::toString(mValue));
        }

        eventValueChanged(mValue);
    }

    void NumericEditBox::setValue(std::int32_t value)
    {
        if (value != mValue)
        {
            setCaption(MyGUI::utility::toString(value));
            mValue = value;
        }
    }

    std::int32_t NumericEditBox::getValue()
    {
        return mValue;
    }

    void NumericEditBox::setMinValue(std::int32_t minValue)
    {
        mMinValue = minValue;
    }

    void NumericEditBox::setMaxValue(std::int32_t maxValue)
    {
        mMaxValue = maxValue;
    }

    void NumericEditBox::onKeyLostFocus(MyGUI::Widget* newWidget)
    {
        Base::onKeyLostFocus(newWidget);
        setCaption(MyGUI::utility::toString(mValue));
    }

    void NumericEditBox::onKeyButtonPressed(MyGUI::KeyCode key, MyGUI::Char character)
    {
        if (key == MyGUI::KeyCode::ArrowUp)
        {
            setValue(std::min(mValue + 1, mMaxValue));
            eventValueChanged(mValue);
        }
        else if (key == MyGUI::KeyCode::ArrowDown)
        {
            setValue(std::max(mValue - 1, mMinValue));
            eventValueChanged(mValue);
        }
        else if (character == 0 || (character >= '0' && character <= '9'))
            Base::onKeyButtonPressed(key, character);
    }

}
