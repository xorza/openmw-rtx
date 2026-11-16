#include "debugwindow.hpp"

#include <mutex>

#include <MyGUI_EditBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/luamanager.hpp>
#include <components/debug/debugging.hpp>
#include <components/settings/values.hpp>

namespace MWGui
{

    DebugWindow::DebugWindow()
        : WindowBase("openmw_debug_window.layout")
    {
        getWidget(mTabControl, "TabControl");

        // Ideas for other tabs:
        // - Texture / compositor texture viewer
        // - Material editor
        // - Shader editor

        MyGUI::TabItem* itemLV = mTabControl->addItem("Log Viewer");
        itemLV->setCaptionWithReplacing(" #{OMWEngine:LogViewer} ");
        mLogView
            = itemLV->createWidgetReal<MyGUI::EditBox>("LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mLogView->setEditReadOnly(true);

        MyGUI::TabItem* itemLuaProfiler = mTabControl->addItem("Lua Profiler");
        itemLuaProfiler->setCaptionWithReplacing(" #{OMWEngine:LuaProfiler} ");
        mLuaProfiler = itemLuaProfiler->createWidgetReal<MyGUI::EditBox>(
            "LogEdit", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Stretch);
        mLuaProfiler->setEditReadOnly(true);
    }

    namespace
    {
        std::vector<char> sLogCircularBuffer;
        std::mutex sBufferMutex;
        int64_t sLogStartIndex;
        int64_t sLogEndIndex;
        bool hasPrefix = false;
    }

    void DebugWindow::startLogRecording()
    {
        sLogCircularBuffer.resize(Settings::general().mLogBufferSize);
        Debug::setLogListener([](Debug::Level level, std::string_view prefix, std::string_view msg) {
            if (sLogCircularBuffer.empty())
                return; // Log viewer is disabled.
            std::string_view color;
            switch (level)
            {
                case Debug::Error:
                    color = "#FF0000";
                    break;
                case Debug::Warning:
                    color = "#FFFF00";
                    break;
                case Debug::Info:
                    color = "#FFFFFF";
                    break;
                case Debug::Verbose:
                case Debug::Debug:
                    color = "#666666";
                    break;
                default:
                    color = "#FFFFFF";
            }
            bool bufferOverflow = false;
            std::scoped_lock lock(sBufferMutex);
            const auto bufSize = static_cast<int64_t>(sLogCircularBuffer.size());
            auto addChar = [&](char c) {
                sLogCircularBuffer[static_cast<std::size_t>(sLogEndIndex++)] = c;
                if (sLogEndIndex == bufSize)
                    sLogEndIndex = 0;
                bufferOverflow = bufferOverflow || sLogEndIndex == sLogStartIndex;
            };
            auto addShieldedStr = [&](std::string_view s) {
                for (char c : s)
                {
                    addChar(c);
                    if (c == '#')
                        addChar(c);
                    if (c == '\n')
                        hasPrefix = false;
                }
            };
            for (char c : color)
                addChar(c);
            if (!hasPrefix)
            {
                addShieldedStr(prefix);
                hasPrefix = true;
            }
            addShieldedStr(msg);
            if (bufferOverflow)
                sLogStartIndex = (sLogEndIndex + 1) % bufSize;
        });
    }

    void DebugWindow::updateLogView()
    {
        std::scoped_lock lock(sBufferMutex);

        if (!mLogView || sLogCircularBuffer.empty() || sLogStartIndex == sLogEndIndex)
            return;
        if (mLogView->isTextSelection())
            return; // Don't change text while player is trying to copy something

        std::string addition;
        const auto bufSize = static_cast<int64_t>(sLogCircularBuffer.size());
        {
            if (sLogStartIndex < sLogEndIndex)
                addition = std::string(sLogCircularBuffer.data() + sLogStartIndex,
                    static_cast<std::size_t>(sLogEndIndex - sLogStartIndex));
            else
            {
                addition = std::string(
                    sLogCircularBuffer.data() + sLogStartIndex, static_cast<std::size_t>(bufSize - sLogStartIndex));
                addition.append(sLogCircularBuffer.data(), static_cast<std::size_t>(sLogEndIndex));
            }
            sLogStartIndex = sLogEndIndex;
        }

        size_t scrollPos = mLogView->getVScrollPosition();
        bool scrolledToTheEnd = scrollPos + 1 >= mLogView->getVScrollRange();
        auto newSizeEstimation = static_cast<int64_t>(mLogView->getTextLength() + addition.size());
        if (newSizeEstimation > bufSize)
            mLogView->eraseText(0, static_cast<std::size_t>(newSizeEstimation - bufSize));
        mLogView->addText(addition);
        if (scrolledToTheEnd && mLogView->getVScrollRange() > 0)
            mLogView->setVScrollPosition(mLogView->getVScrollRange() - 1);
        else
            mLogView->setVScrollPosition(scrollPos);
    }

    void DebugWindow::updateLuaProfile()
    {
        if (mLuaProfiler->isTextSelection())
            return;

        size_t previousPos = mLuaProfiler->getVScrollPosition();
        mLuaProfiler->setCaption(MWBase::Environment::get().getLuaManager()->formatResourceUsageStats());
        mLuaProfiler->setVScrollPosition(std::min(previousPos, mLuaProfiler->getVScrollRange() - 1));
    }

    void DebugWindow::onFrame(float dt)
    {
        static float timer = 0;
        timer -= dt;
        if (timer > 0 || !isVisible())
            return;
        timer = 0.25;

        switch (mTabControl->getIndexSelected())
        {
            case 0:
                updateLogView();
                break;
            case 1:
                updateLuaProfile();
                break;
            default:;
        }
    }
}
