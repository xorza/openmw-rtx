#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include <MyGUI_Colour.h>

#include <components/widgets/box.hpp>

namespace MWGui
{
    namespace Formatting
    {
        struct TextStyle
        {
            TextStyle()
                : mColour(0, 0, 0)
                , mFont("Journalbook DefaultFont")
                , mTextSize(16)
            {
            }

            MyGUI::Colour mColour;
            std::string mFont;
            std::int32_t mTextSize;
        };

        struct BlockStyle
        {
            BlockStyle()
                : mAlign(MyGUI::Align::Left | MyGUI::Align::Top)
            {
            }

            MyGUI::Align mAlign;
        };

        class BookTextParser
        {
        public:
            typedef std::map<std::string, std::string> Attributes;
            enum Events
            {
                Event_None = -2,
                Event_EOF = -1,
                Event_BrTag,
                Event_PTag,
                Event_ImgTag,
                Event_DivTag,
                Event_FontTag,
                Event_PageBreak,
            };

            BookTextParser(std::string text, bool shrinkTextAtLastTag);

            Events next();

            const Attributes& getAttributes() const;
            std::string getReadyText() const;
            bool isClosingTag() const;

        private:
            void registerTag(const std::string& tag, Events type);
            void flushBuffer();
            void parseTag(std::string tag);

            std::size_t mIndex;
            std::string mText;
            std::string mReadyText;

            bool mIgnoreNewlineTags;
            bool mIgnoreLineEndings;
            Attributes mAttributes;
            std::string mTag;
            bool mClosingTag;
            std::map<std::string, Events> mTagTypes;
            std::string mBuffer;

            std::size_t mPlainTextEnd;
        };

        class Paginator
        {
        public:
            typedef std::pair<std::int32_t, std::int32_t> Page;
            typedef std::vector<Page> Pages;

            Paginator(std::int32_t pageWidth, std::int32_t pageHeight)
                : mStartTop(0)
                , mCurrentTop(0)
                , mPageWidth(pageWidth)
                , mPageHeight(pageHeight)
                , mIgnoreLeadingEmptyLines(false)
            {
            }

            std::int32_t getStartTop() const { return mStartTop; }
            std::int32_t getCurrentTop() const { return mCurrentTop; }
            std::int32_t getPageWidth() const { return mPageWidth; }
            std::int32_t getPageHeight() const { return mPageHeight; }
            bool getIgnoreLeadingEmptyLines() const { return mIgnoreLeadingEmptyLines; }
            Pages getPages() const { return mPages; }

            void setStartTop(std::int32_t top) { mStartTop = top; }
            void setCurrentTop(std::int32_t top) { mCurrentTop = top; }
            void setIgnoreLeadingEmptyLines(bool ignore) { mIgnoreLeadingEmptyLines = ignore; }

            Paginator& operator<<(const Page& page)
            {
                mPages.push_back(page);
                return *this;
            }

        private:
            std::int32_t mStartTop, mCurrentTop;
            std::int32_t mPageWidth, mPageHeight;
            bool mIgnoreLeadingEmptyLines;
            Pages mPages;
        };

        /// \brief utilities for parsing book/scroll text as mygui widgets
        class BookFormatter
        {
        public:
            Paginator::Pages markupToWidget(MyGUI::Widget* parent, const std::string& markup,
                const std::int32_t pageWidth, const std::int32_t pageHeight, bool shrinkTextAtLastTag);
            Paginator::Pages markupToWidget(MyGUI::Widget* parent, const std::string& markup, bool shrinkTextAtLastTag);

        private:
            void resetFontProperties();

            void handleDiv(const BookTextParser::Attributes& attr);
            void handleFont(const BookTextParser::Attributes& attr);

            TextStyle mTextStyle;
            BlockStyle mBlockStyle;
        };

        class GraphicElement
        {
        public:
            GraphicElement(MyGUI::Widget* parent, Paginator& pag, const BlockStyle& blockStyle);
            virtual std::int32_t getHeight() = 0;
            virtual void paginate();
            virtual std::int32_t pageSplit();

        protected:
            virtual ~GraphicElement() = default;
            MyGUI::Widget* mParent;
            Paginator& mPaginator;
            BlockStyle mBlockStyle;
        };

        class TextElement : public GraphicElement
        {
        public:
            TextElement(MyGUI::Widget* parent, Paginator& pag, const BlockStyle& blockStyle, TextStyle textStyle,
                const std::string& text);
            std::int32_t getHeight() override;
            std::int32_t pageSplit() override;

        private:
            std::int32_t currentFontHeight() const;
            TextStyle mTextStyle;
            Gui::EditBox* mEditBox;
        };

        class ImageElement : public GraphicElement
        {
        public:
            ImageElement(MyGUI::Widget* parent, Paginator& pag, const BlockStyle& blockStyle, const std::string& src,
                std::int32_t width, std::int32_t height);
            std::int32_t getHeight() override;
            std::int32_t pageSplit() override;

        private:
            std::int32_t mImageHeight;
            MyGUI::ImageBox* mImageBox;
        };
    }
}
