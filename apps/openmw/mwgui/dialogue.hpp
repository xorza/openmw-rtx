#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <MyGUI_Delegate.h>

#include <apps/openmw/mwbase/dialoguemanager.hpp>
#include <apps/openmw/mwdialogue/keywordsearch.hpp>

#include "bookpage.hpp"
#include "referenceinterface.hpp"
#include "windowbase.hpp"

namespace Gui
{
    class AutoSizedTextBox;
    class MWList;
}

namespace MWGui
{
    class DialogueWindow;

    class ResponseCallback : public MWBase::DialogueManager::ResponseCallback
    {
        DialogueWindow* mWindow;
        bool mNeedMargin;

    public:
        explicit ResponseCallback(DialogueWindow* win, bool needMargin = true)
            : mWindow(win)
            , mNeedMargin(needMargin)
        {
        }

        void addResponse(std::string_view title, std::string_view text) override;

        void updateTopics() const;
    };

    class PersuasionDialog : public WindowModal
    {
    public:
        explicit PersuasionDialog(std::unique_ptr<ResponseCallback> callback);

        void onOpen() override;

        MyGUI::Widget* getDefaultKeyFocus() override;

    protected:
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

    private:
        std::unique_ptr<ResponseCallback> mCallback;

        std::int32_t mInitialGoldLabelWidth;
        std::int32_t mInitialMainWidgetWidth;

        MyGUI::Button* mCancelButton{};
        MyGUI::Button* mAdmireButton{};
        MyGUI::Button* mIntimidateButton{};
        MyGUI::Button* mTauntButton{};
        MyGUI::Button* mBribe10Button{};
        MyGUI::Button* mBribe100Button{};
        MyGUI::Button* mBribe1000Button{};
        MyGUI::Widget* mActionsBox{};
        Gui::AutoSizedTextBox* mGoldLabel{};

        std::vector<MyGUI::Button*> mButtons;
        std::size_t mControllerFocus = 0;

        void adjustAction(MyGUI::Widget* action, std::int32_t& totalHeight);

        void onCancel(MyGUI::Widget* sender);
        void onPersuade(MyGUI::Widget* sender);
    };

    struct Link
    {
        virtual ~Link() = default;
        virtual void activated() = 0;
    };

    struct Topic : Link
    {
        MyGUI::delegates::MultiDelegate<const std::string&> eventTopicActivated;
        explicit Topic(std::string id)
            : mTopicId(std::move(id))
        {
        }
        std::string mTopicId;
        void activated() override;
    };

    struct Choice : Link
    {
        MyGUI::delegates::MultiDelegate<std::int32_t> eventChoiceActivated;
        explicit Choice(std::int32_t id)
            : mChoiceId(id)
        {
        }
        std::int32_t mChoiceId;
        void activated() override;
    };

    struct Goodbye : Link
    {
        MyGUI::delegates::MultiDelegate<> eventActivated;
        void activated() override;
    };

    struct DialogueText
    {
        virtual ~DialogueText() = default;
        virtual void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const = 0;
        std::string mText;
    };

    struct Response : DialogueText
    {
        explicit Response(std::string_view text, std::string_view title = {}, bool needMargin = true);
        void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const override;
        std::string mTitle;
        bool mNeedMargin;
    };

    struct Message : DialogueText
    {
        explicit Message(std::string_view text);
        void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const override;
    };

    class DialogueWindow : public WindowBase, public ReferenceInterface
    {
    public:
        DialogueWindow();

        void onTradeComplete();

        bool exit() override;

        void notifyLinkClicked(TypesetBook::InteractiveId link);

        void setPtr(const MWWorld::Ptr& actor) override;

        /// @return true if stale keywords were updated successfully
        bool setKeywords(const std::list<std::string>& keyWord);

        void addResponse(std::string_view title, std::string_view text, bool needMargin = true);

        void addMessageBox(std::string_view text);

        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        void updateTopics();

        void onClose() override;

        std::string_view getWindowIdForLua() const override { return "Dialogue"; }

    protected:
        void updateTopicsPane();
        bool isCompanion(const MWWorld::Ptr& actor);
        bool isCompanion();

        void onSelectListItem(const std::string& topic, std::int32_t id);
        void onByeClicked(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, std::int32_t rel);
        void onWindowResize(MyGUI::Window* sender);
        void onTopicActivated(const std::string& topicId);
        void onChoiceActivated(std::int32_t id);
        void onGoodbyeActivated();

        void onScrollbarMoved(MyGUI::ScrollBar* sender, std::size_t pos);

        void updateHistory(bool scrollbar = false);

        void onReferenceUnavailable() override;

        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

    private:
        void updateDisposition();
        void restock();
        void deleteLater();
        void redrawTopicsList();

        bool mIsCompanion;
        std::list<std::string> mKeywords;

        std::vector<std::unique_ptr<DialogueText>> mHistoryContents;
        std::vector<std::pair<std::string, std::int32_t>> mChoices;
        std::vector<BookTypesetter::Style*> mChoiceStyles;
        bool mGoodbye;

        std::vector<std::unique_ptr<Link>> mLinks;
        std::unordered_map<std::string, std::unique_ptr<Link>> mTopicLinks;

        std::vector<std::unique_ptr<Link>> mDeleteLater;

        MWDialogue::KeywordSearch mKeywordSearch;

        BookPage* mHistory{};
        Gui::MWList* mTopicsList{};
        MyGUI::ScrollBar* mScrollBar{};
        MyGUI::ProgressBar* mDispositionBar{};
        MyGUI::TextBox* mDispositionText{};
        MyGUI::Button* mGoodbyeButton{};

        PersuasionDialog mPersuasionDialog;

        MyGUI::IntSize mCurrentWindowSize;

        std::unique_ptr<ResponseCallback> mCallback;
        std::unique_ptr<ResponseCallback> mGreetingCallback;

        void setControllerFocus(std::size_t index, bool focused);
        std::size_t mControllerFocus = 0;
        std::int32_t mControllerChoice = -1;

        void updateTopicFormat();
    };
}
