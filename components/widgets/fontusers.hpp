#pragma once

#include <cstddef>
#include <vector>

namespace MyGUI
{
    class IObject;
}

namespace Gui
{
    /// **Everything drawn with the interface's fonts, known by construction**: each user enters this
    /// as it is made and leaves as it is destroyed, so a refit after the fonts are rasterized again
    /// reaches every one. A font's texture is replaced by its rasterization, and a user a refit
    /// missed keeps the destroyed one (`docs/research/2026-10-31-mygui-font-lifetimes.md`).
    ///
    /// The texts are known through their factory: the fork's texts stand in MyGUI's sub-widget factories
    /// under MyGUI's names, `EditText` and `SimpleText` (`Users::Texts`), so every text any widget, skin,
    /// popup or script makes is one. A walk of the widget tree would miss a skin's own children and the
    /// widgets on no layer, and nothing at the draw can catch a missed text, which is drawn from its
    /// render item's vertices without being asked.
    ///
    /// Pinned: what it holds and the factories MyGUI keeps call back into it. It outlives every
    /// user, the last ones leaving at MyGUI's shutdown.
    class FontUsers
    {
    public:
        /// What the registry knows of a user.
        class Entry
        {
        public:
            Entry(const Entry&) = delete;
            Entry& operator=(const Entry&) = delete;

            /// Lets go of whatever holds a font's old texture, before any user is refitted.
            virtual void drop() {}

            /// Takes its fonts again, once every user has dropped.
            virtual void refit() = 0;

        protected:
            explicit Entry(FontUsers& users);
            virtual ~Entry();

            const FontUsers& getUsers() const { return mUsers; }

        private:
            friend class FontUsers;

            FontUsers& mUsers;

            /// Where it stands among the registry's entries, so it leaves without a search.
            std::size_t mIndex;
        };

        /// Which users a registry holds, fixed as it is made, and so which of MyGUI's factories it
        /// stands in.
        enum class Users
        {
            /// The texts, through `EditText` and `SimpleText`.
            Texts,

            /// The lists (`Gui::ListBox`), through `ListBox`. A list's refit makes lines, and so
            /// texts: the lists' registry is not the texts'.
            Lists,

            /// What its owner enters by hand, as a book page does: no factory.
            Entered,
        };

        /// Stands its factories, for `Texts` and `Lists`, in place of MyGUI's own, each user
        /// entering this as it is made: once the factory manager exists, and before a skin or a
        /// layout is loaded.
        FontUsers(Users users, int textHeight);

        ~FontUsers();

        FontUsers(const FontUsers&) = delete;
        FontUsers& operator=(const FontUsers&) = delete;
        FontUsers(FontUsers&&) = delete;
        FontUsers& operator=(FontUsers&&) = delete;

        /// Every user drops, then every user is refitted at `textHeight`: after a font is rasterized
        /// again or set at another size, and before the next frame draws. No user of this registry
        /// is made or destroyed meanwhile, which an assert holds to; a refit may make users of
        /// another.
        void refit(int textHeight);

        /// The height a user with none of its own lays its text out at, in layout units: the size
        /// the fonts are set at (`FontFit::mFontSize`).
        int getTextHeight() const { return mTextHeight; }

        std::size_t size() const { return mEntries.size(); }

    private:
        void makeText(MyGUI::IObject*& made);
        void makeSimpleText(MyGUI::IObject*& made);
        void makeList(MyGUI::IObject*& made);

        std::vector<Entry*> mEntries;
        int mTextHeight;
        bool mRefitting = false;
    };
}
