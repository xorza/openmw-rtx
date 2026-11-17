#pragma once

#include <cstddef>
#include <memory>

#include <components/toutf8/toutf8.hpp>

#include "windowbase.hpp"

namespace MWBase
{
    class WindowManager;
}

namespace MWGui
{
    struct JournalViewModel;

    struct JournalWindow : public BookWindowBase
    {
        JournalWindow();

        /// construct a new instance of the one JournalWindow implementation
        static std::unique_ptr<JournalWindow> create(
            const std::shared_ptr<JournalViewModel>& model, bool questList, ToUTF8::FromType encoding);

        /// destroy this instance of the JournalWindow implementation
        ~JournalWindow() override = default;

        /// show/hide the journal window
        void setVisible(bool newValue) override = 0;

        std::string_view getWindowIdForLua() const override { return "Journal"; }

        std::vector<MyGUI::Button*> mButtons;
        std::size_t mIndexRowCount = 1;
        std::size_t mSelectedQuest = 0;
        std::size_t mSelectedIndex = 0;
    };
}
