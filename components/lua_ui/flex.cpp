#include "flex.hpp"

#include <cstddef>
#include <cstdint>

namespace LuaUi
{
    void LuaFlex::updateProperties()
    {
        mGap = propertyValue("gap", 0);
        mWrap = propertyValue("wrap", false);
        mHorizontal = propertyValue("horizontal", false);
        mAutoSized = propertyValue("autoSize", true);
        mAlign = propertyValue("align", Alignment::Start);
        mArrange = propertyValue("arrange", Alignment::Start);
        WidgetExtension::updateProperties();
    }

    namespace
    {
        std::int32_t alignSize(std::int32_t container, std::int32_t content, Alignment alignment)
        {
            switch (alignment)
            {
                case Alignment::Start:
                    return 0;
                case Alignment::Center:
                    return (container - content) / 2;
                case Alignment::End:
                    return container - content;
            }
            return 0;
        }

        float getGrow(WidgetExtension* w)
        {
            return std::max(0.0f, w->externalValue("grow", 0.0f));
        }
    }

    void LuaFlex::updateChildren()
    {
        const auto& flexChildren = children();

        MyGUI::IntSize childrenSize;
        if (mAutoSized)
        {
            std::int32_t measuredPrimary = 0;
            std::int32_t measuredSecondary = 0;
            bool isFirst = true;
            for (auto* w : flexChildren)
            {
                w->clearForced();
                const MyGUI::IntSize size = w->calculateSize();
                measuredPrimary += primary(size) + (isFirst ? 0 : mGap);
                measuredSecondary = std::max(measuredSecondary, secondary(size));
                isFirst = false;
            }
            primary(childrenSize) = measuredPrimary;
            secondary(childrenSize) = measuredSecondary;
            mChildrenSize = childrenSize;
        }

        MyGUI::IntSize flexSize = calculateSize();

        std::int32_t currentSecondaryAxisPosition = 0;
        std::size_t widgetIndex = 0;

        while (widgetIndex < flexChildren.size())
        {
            const std::size_t trackStart = widgetIndex;
            std::int32_t primaryAxisSize = 0;
            std::int32_t primaryAxisSizeRemaining = primary(flexSize);
            std::int32_t secondaryAxisSize = 0;
            float totalPrimaryGrow = 0;

            while (widgetIndex < flexChildren.size())
            {
                auto* w = flexChildren[widgetIndex];
                w->clearForced();

                const MyGUI::IntSize size = w->calculateSize();
                const std::int32_t childPrimary = primary(size);
                const std::int32_t childSecondary = secondary(size);
                const bool notFirstOnTrack = (widgetIndex > trackStart);
                const std::int32_t primaryToAdd = childPrimary + (notFirstOnTrack ? mGap : 0);

                if (!mAutoSized && mWrap && notFirstOnTrack && primaryAxisSizeRemaining < primaryToAdd)
                    break;

                primaryAxisSize += primaryToAdd;
                primaryAxisSizeRemaining -= primaryToAdd;
                secondaryAxisSize = std::max(childSecondary, secondaryAxisSize);
                totalPrimaryGrow += getGrow(w);
                widgetIndex++;
            }
            primaryAxisSizeRemaining = std::max(0, primaryAxisSizeRemaining);

            // Keeping a constant total here ensures widgets that use grow don't use the
            // changing 'primaryAxisSizeRemaining' (which shrinks as we take off grown amounts).
            const std::int32_t totalPrimaryAxisSizeRemaining = primaryAxisSizeRemaining;
            const std::int32_t trackPrimarySizeAfterGrow
                = primaryAxisSize + (totalPrimaryGrow > 0 ? totalPrimaryAxisSizeRemaining : 0);
            const std::int32_t primaryAxisChildrenShift
                = alignSize(primary(flexSize), trackPrimarySizeAfterGrow, mAlign);
            std::int32_t currentPrimaryAxisPosition = primaryAxisChildrenShift;

            for (std::size_t j = trackStart; j < widgetIndex; ++j)
            {
                auto* w = flexChildren[j];
                MyGUI::IntPoint widgetPosition;
                MyGUI::IntSize widgetSize = w->calculateSize();

                // Note: Grow is on the primary axis and stretch is "grow" on the cross/secondary axis
                const float widgetGrowFactor = getGrow(w);
                const float stretch = std::clamp(w->externalValue("stretch", 0.0f), 0.0f, 1.0f);
                if (widgetGrowFactor > 0)
                {
                    const std::int32_t pixelsToExpandBy
                        = std::clamp(static_cast<std::int32_t>(std::round((widgetGrowFactor / totalPrimaryGrow)
                                         * static_cast<float>(totalPrimaryAxisSizeRemaining))),
                            0, primaryAxisSizeRemaining);
                    primary(widgetSize) += pixelsToExpandBy;
                    primaryAxisSizeRemaining -= pixelsToExpandBy;
                }

                const std::int32_t stretchTargetSecondary = mWrap ? secondaryAxisSize : secondary(flexSize);
                secondary(widgetSize) = std::max(secondary(widgetSize),
                    std::clamp(
                        static_cast<std::int32_t>(std::round(stretch * static_cast<float>(stretchTargetSecondary))), 0,
                        stretchTargetSecondary));

                primary(widgetPosition) = currentPrimaryAxisPosition;
                currentPrimaryAxisPosition += primary(widgetSize) + mGap;
                const std::int32_t arrangeTargetSecondary = mWrap ? secondaryAxisSize : secondary(flexSize);
                secondary(widgetPosition)
                    = currentSecondaryAxisPosition + alignSize(arrangeTargetSecondary, secondary(widgetSize), mArrange);

                w->forceSize(widgetSize);
                w->forcePosition(widgetPosition);
                w->updateCoord();
            }

            currentSecondaryAxisPosition += secondaryAxisSize + mGap;
        }

        mChildrenSize = childrenSize;
        WidgetExtension::updateChildren();
    }

    MyGUI::IntSize LuaFlex::childScalingSize() const
    {
        // Call the base method to prevent relativeSize feedback loop
        MyGUI::IntSize size = WidgetExtension::calculateSize();
        if (mAutoSized)
            primary(size) = 0;
        return size;
    }

    MyGUI::IntSize LuaFlex::calculateSize() const
    {
        MyGUI::IntSize size = WidgetExtension::calculateSize();
        if (mAutoSized)
        {
            primary(size) = std::max(primary(size), primary(mChildrenSize));
            secondary(size) = std::max(secondary(size), secondary(mChildrenSize));
        }
        return size;
    }

    void LuaFlex::updateCoord()
    {
        updateChildren();
        WidgetExtension::updateCoord();
    }

    const std::vector<std::string_view>& LuaFlex::allUsedProperties() const
    {
        static std::vector<std::string_view> usedProps = std::invoke([this] {
            std::vector<std::string_view> props = { "horizontal", "autoSize", "arrange", "align", "gap", "wrap" };
            auto baseProps = WidgetExtension::allUsedProperties();
            props.insert(props.end(), baseProps.begin(), baseProps.end());
            return props;
        });
        return usedProps;
    }
}
